/* ex: set ts=4 et: */


#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_general.h"
#include "util_filter.h"
#include "apr_buckets.h"
#include "http_request.h"
#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "imgmin.h"

static const char imgminFilterName[] = "IMGMIN";
module AP_MODULE_DECLARE_DATA imgmin_module;

/* per-... ? */
typedef struct {
    struct imgmin_options opt;
    apr_size_t bufferSize;
} imgmin_filter_config;

/*
 * NOTE: we need the entire file in a contiguous buffer (can ImageMagick handle a stream?)
 * and as far as I know the contents may come in multiple buckets, so we hardcode an
 * upper limit, allocate a buffer and append into it until we see the EOS, then
 * process it and pass it on
 */
#define DEFAULT_BUFFERSIZE (4 * 1024 * 1024)

static void *create_imgmin_server_config(apr_pool_t *p, server_rec *s)
{
    imgmin_filter_config *c = apr_pcalloc(p, sizeof *c);

    (void) imgmin_options_init(&c->opt);
    c->bufferSize = DEFAULT_BUFFERSIZE;
    /* intialize ImageMagick */
    MagickWandGenesis();
    return c;
}

static const char *imgmin_set_error_threshold(cmd_parms *cmd,
                                              void *dummy,
                                              const char *arg)
{
    imgmin_filter_config *c = ap_get_module_config(
                                cmd->server->module_config,
                                &imgmin_module);
    imgmin_opt_set_error_threshold(&c->opt, arg);
    return NULL;
}

typedef struct imgmin_ctx_t
{
    apr_bucket_brigade *bb;
    unsigned char *buffer;
    apr_size_t buflen;
} imgmin_ctx;

static apr_status_t imgmin_ctx_cleanup(void *data)
{
    imgmin_ctx *ctx = data;
    return APR_SUCCESS;
}

static void magickfree(void *data)
{
    (void) MagickRelinquishMemory(data);
}

/*
 * image file blob is in ctx->buffer[0..ctx->buflen)
 * read it into imagemagick, apply imgmin to it and append the resulting blob
 * into ctx->bb brigade
 */
static void do_imgmin(ap_filter_t *f, imgmin_ctx *ctx, imgmin_filter_config *c)
{
    MagickWand *mw = NewMagickWand();
    MagickReadImageBlob(mw, ctx->buffer, ctx->buflen);
    /*
     * FIXME TODO: implement per-file caching via content checksumming
     */
    {
        MagickWand *tmp = search_quality(mw, "-", &c->opt);
        size_t newsize = ctx->buflen + 1;
        unsigned char *blob = MagickGetImageBlob(tmp, &newsize);
        if (newsize > ctx->buflen)
        {
            (void) MagickRelinquishMemory(blob);
            blob = MagickGetImageBlob(mw, &newsize);
        }
        {
            apr_bucket *b = apr_bucket_heap_create((char *)blob,
                                                   newsize, magickfree,
                                                   f->c->bucket_alloc);
            APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
        }
        DestroyMagickWand(tmp);
    }
    DestroyMagickWand(mw);
}

static apr_status_t imgmin_out_filter(ap_filter_t *f,
                                      apr_bucket_brigade *bb)
{
    apr_bucket *e;
    request_rec *r = f->r;
    imgmin_ctx *ctx = f->ctx;
    imgmin_filter_config *c;

    /* Do nothing if asked to filter nothing. */
    if (APR_BRIGADE_EMPTY(bb)) {
        return ap_pass_brigade(f->next, bb);
    }

    c = ap_get_module_config(r->server->module_config, &imgmin_module);

    /* If we don't have a context, we need to ensure that it is okay to send
     * the imgmind content.  If we have a context, that means we've done
     * this before and we liked it.
     * This could be not so nice if we always fail.  But, if we succeed,
     * we're in better shape.
     */
    if (!ctx) {
        char *token;
        const char *encoding;

        /* only work on main request/no subrequests */
        if (r->main != NULL) {
            ap_remove_output_filter(f);
            return ap_pass_brigade(f->next, bb);
        }

        /* We can't operate on Content-Ranges */
        if (apr_table_get(r->headers_out, "Content-Range") != NULL) {
            ap_remove_output_filter(f);
            return ap_pass_brigade(f->next, bb);
        }

        /* For a 304 or 204 response there is no entity included in
         * the response and hence nothing to imgmin. */
        if (r->status == HTTP_NOT_MODIFIED || r->status == HTTP_NO_CONTENT) {
            ap_remove_output_filter(f);
            return ap_pass_brigade(f->next, bb);
        }

        /* We're cool with filtering this. */
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));
        ctx->bb = apr_brigade_create(r->pool, f->c->bucket_alloc);
        ctx->buffer = apr_palloc(r->pool, c->bufferSize);
        ctx->buflen = 0;

        /* initialize... */

        /*
         * Register a cleanup function to ensure that we cleanup
         * imgmin resources.
         */
        apr_pool_cleanup_register(r->pool, ctx, imgmin_ctx_cleanup,
                                  apr_pool_cleanup_null);

    }

    while (!APR_BRIGADE_EMPTY(bb))
    {
        e = APR_BRIGADE_FIRST(bb);

        if (APR_BUCKET_IS_EOS(e)) {
            /* data is in ctx->buffer, imgmin it, pass results to new
             * brigade ctx->bb */

            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                          "pre-do_imgmin buflen: %lu",
                          (unsigned long)ctx->buflen);

            do_imgmin(f, ctx, c);

            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "post do_imgmin");

            /* No need for cleanup any longer */
            apr_pool_cleanup_kill(r->pool, ctx, imgmin_ctx_cleanup);

            /* Remove EOS from the old list, and insert into the new. */
            APR_BUCKET_REMOVE(e);
            APR_BRIGADE_INSERT_TAIL(ctx->bb, e);

            /* Okay, we've seen the EOS.
             * Time to pass it along down the chain.
             */
            return ap_pass_brigade(f->next, ctx->bb);
        }

        if (APR_BUCKET_IS_FLUSH(e)) {
            apr_status_t rv;
            /* Remove flush bucket from old brigade and insert into the new. */
            APR_BUCKET_REMOVE(e);
            APR_BRIGADE_INSERT_TAIL(ctx->bb, e);
            rv = ap_pass_brigade(f->next, ctx->bb);
            if (rv != APR_SUCCESS) {
                return rv;
            }
            continue;
        }

        if (APR_BUCKET_IS_METADATA(e)) {
            /* Remove old brigade, insert into new. */
            APR_BUCKET_REMOVE(e);
            APR_BRIGADE_INSERT_TAIL(ctx->bb, e);
            continue;
        }

        /* append bucket data to ctx->buffer... */
        {
            const char *data = NULL;
            apr_size_t len = 0;

            apr_bucket_read(e, &data, &len, APR_BLOCK_READ);
            if (c->bufferSize - ctx->buflen < len)
            {
                len = c->bufferSize - ctx->buflen;
            }
            memcpy(ctx->buffer + ctx->buflen, data, len);
            ctx->buflen += len;
        }

        apr_bucket_delete(e);
    }

    apr_brigade_cleanup(bb);
    return APR_SUCCESS;
}

#define PROTO_FLAGS AP_FILTER_PROTO_CHANGE|AP_FILTER_PROTO_CHANGE_LENGTH
static void register_hooks(apr_pool_t *p)
{
    ap_register_output_filter(imgminFilterName, imgmin_out_filter,  NULL, AP_FTYPE_CONTENT_SET);
    //ap_hook_do_something(my_something_doer, NULL, NULL, APR_HOOK_MIDDLE);
#if 0
    ap_register_output_filter("INFLATE",        inflate_out_filter, NULL, AP_FTYPE_RESOURCE-1);
    ap_register_input_filter(imgminFilterName,  imgmin_in_filter,   NULL, AP_FTYPE_CONTENT_SET);
#endif
}

static const command_rec imgmin_filter_cmds[] = {
    AP_INIT_TAKE1("ImgminErrorThreshold",      imgmin_set_error_threshold, NULL, RSRC_CONF, "See error threshold (0-255.0)"),
    {NULL}
};

module AP_MODULE_DECLARE_DATA imgmin_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                         /* dir config creater                    */
    NULL,                         /* dir merger --- default is to override */
    create_imgmin_server_config,  /* server config                         */
    NULL,                         /* merge server config                   */
    imgmin_filter_cmds,           /* command table                         */
    register_hooks                /* register hooks                        */
};
