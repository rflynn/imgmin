/* ex: set ts=4 et: */
/*
 * IMGMIN Apache2 Module
 *
 * Author: Ryan Flynn <parseerror@gmail.com>
 * http://github.com/rflynn
 */

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_general.h"
#include "util_filter.h"
#include "apr_buckets.h"
#include "apr_md5.h"
#include "http_request.h"
#define APR_WANT_STRFUNC
#include "apr_want.h"

/* mkdir */
#include <sys/stat.h>
#include <sys/types.h>

#include "imgmin.h"

module AP_MODULE_DECLARE_DATA imgmin_module;

/* per-... ? */
typedef struct {
    struct imgmin_options opt;
    apr_size_t bufferSize;
    char cache_dir[PATH_MAX];
} imgmin_filter_config;

/*
 * default/min/max config settings
 * see 'imgmin_filter_cmds' at the bottom of the file for the keys to override this in httpd.conf
 */
#define CACHE_DIR_DEFAULT   "/var/imgmin-cache"
#define BUFFERSIZE_DEFAULT (1024 * 1024 * 4)
#define BUFFERSIZE_MIN     (1024 * 256) /* anything less than this is stupid */

static void *create_imgmin_server_config(apr_pool_t *p, server_rec *s)
{
    imgmin_filter_config *c = apr_pcalloc(p, sizeof *c);

    (void) imgmin_options_init(&c->opt);
    c->bufferSize = BUFFERSIZE_DEFAULT;
    strcpy(c->cache_dir, CACHE_DIR_DEFAULT);
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

static const char *imgmin_set_cache_dir(cmd_parms *cmd,
                                        void *dummy,
                                        const char *arg)
{
    imgmin_filter_config *c = ap_get_module_config(
                                cmd->server->module_config,
                                &imgmin_module);
    if (strlen(arg) >= sizeof c->cache_dir)
    {
        fprintf(stderr, "Cache Dir length > %lu! '%s'\n",
            sizeof c->cache_dir, arg);
    } else {
        strcpy(c->cache_dir, arg);
    }
    return NULL;
}

static const char *imgmin_set_buffer_size(cmd_parms *cmd,
                                          void *dummy,
                                          const char *arg)
{
    imgmin_filter_config *c = ap_get_module_config(
                                cmd->server->module_config,
                                &imgmin_module);
    apr_int64_t n = apr_strtoi64(arg, NULL, 10);
    if (errno == ERANGE) {
        fprintf(stderr, "Invalid bufferSize: %lu\n", (unsigned long)n);
    } else if (n < 64 * 1024) {
        fprintf(stderr, "bufferSize too small: %lu\n", (unsigned long)n);
    } else {
        c->bufferSize = n;
    }
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
    return APR_SUCCESS;
}

static void magickfree(void *data)
{
    (void) MagickRelinquishMemory(data);
}

static char * cache_path(unsigned char *blob, size_t len, char path[PATH_MAX], const char *prefix)
{
    unsigned char digest[APR_MD5_DIGESTSIZE];
    char *result = NULL;

    if (apr_md5(digest, blob, len) == APR_SUCCESS)
    {
        int fmt;

        fmt = snprintf(path, PATH_MAX,
            "%s/%02x/%02x/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
            prefix,
            digest[0], digest[1], digest[2], digest[3],
            digest[4], digest[5], digest[6], digest[7],
            digest[8], digest[9], digest[10], digest[11],
            digest[12], digest[13], digest[14], digest[15]);
        if (fmt >= 0 && (size_t)fmt < PATH_MAX)
        {
            result = path;
        }
    }
    return result;
}

static int mkdir_n(const char path[PATH_MAX], size_t len)
{
    char tmppath[PATH_MAX];

    strcpy(tmppath, path);
    tmppath[len] = '\0';
    fprintf(stderr, "mkdir(%s)\n", tmppath);
    if (mkdir(tmppath, 0755) != 0)
    {
        if (errno != EEXIST)
        {
            perror("mkdir");
        }
        return 0;
    }
    return 1;
}

/*
 * given a path of prefix/XX/YY/ZZZZZZZZZZZZZZZ
 *  ensure that prefix/XX/YY exists
 */
static void cache_path_ensure(const char *prefix, const char path[PATH_MAX])
{
    size_t prelen = strlen(prefix);
    (void) mkdir_n(path, prelen+3);
    (void) mkdir_n(path, prelen+3+3);
}

static void cache_set(const char *prefix, const char *path, unsigned char *blob, size_t len)
{
    FILE *fd;

    cache_path_ensure(prefix, path);
    fd = fopen(path, "wb");
    if (!fd)
    {
        perror("fopen");
    } else {
        if (fwrite(blob, 1, len, fd) != len)
        {
            /* write to an error log...? */
            perror("fwrite");
        }
        fclose(fd);
    }
}

/*
 * given an image in a buffer, attempt to locate and retrieve it in our cache.
 * upon error or the file not being found return NULL
 */
static MagickWand * cache_get(const char *path, imgmin_ctx *ctx)
{
    MagickWand *cached = NULL;
    FILE *fd;

    fd = fopen(path, "rb");
    if (fd)
    {
        cached = NewMagickWand();
        if (cached)
        {
            if (MagickTrue != MagickReadImageFile(cached, fd))
            {
                perror("MagickReadImageFile");
                cached = DestroyMagickWand(cached);
            }
        } else {
            perror("NewMagickWand");
        }
        fclose(fd);
    }
    return cached;
}

/*
 * image file blob is in ctx->buffer[0..ctx->buflen)
 * read it into imagemagick, apply imgmin to it and append the resulting blob
 * into ctx->bb brigade
 */
static void do_imgmin(ap_filter_t *f, imgmin_ctx *ctx, imgmin_filter_config *c)
{
    char path[PATH_MAX];
    MagickWand *mw;
    unsigned char *blob;
    size_t bloblen;

    /*
     * calculate the cache path based on the original image contents
     * and attempt to load its cached results
     */
    mw = NULL;
    if (cache_path(ctx->buffer, ctx->buflen, path, c->cache_dir))
    {
        mw = cache_get(path, ctx);
    }

    if (mw)
    {
        blob = MagickGetImageBlob(mw, &bloblen);
    } else {
        /*
         * not in cache. generate result and save to cache.
         */
        MagickWand *tmp;
        mw = NewMagickWand();
        MagickReadImageBlob(mw, ctx->buffer, ctx->buflen);
        tmp = search_quality(mw, "-", &c->opt);
        blob = MagickGetImageBlob(tmp, &bloblen);
        /* if result is larger than original fall back */
        if (bloblen > ctx->buflen)
        {
            (void) MagickRelinquishMemory(blob);
            blob = MagickGetImageBlob(mw, &bloblen);
        }
        /*
         * if the results aren't from the cache, write to the cache for later use
         */
        cache_set(c->cache_dir, path, blob, bloblen);
        tmp = DestroyMagickWand(tmp);
    }
    /*
     *  by this point we've got the contents of our image response in 'blob',
     *  whether it's a cached image, a new response or the original image.
     */
    {
        apr_bucket *b = apr_bucket_heap_create((char *)blob,
                                               bloblen, magickfree,
                                               f->c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
    }
    mw = DestroyMagickWand(mw);
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

            do_imgmin(f, ctx, c);

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
            if (ctx->buflen + len > c->bufferSize)
            {
                len = c->bufferSize - ctx->buflen;
            }
            memcpy(ctx->buffer + ctx->buflen, data, len);
            ctx->buflen += len;

            APR_BUCKET_REMOVE(e);
        }

        apr_bucket_delete(e);
    }

    apr_brigade_cleanup(bb);
    return APR_SUCCESS;
}

#define PROTO_FLAGS AP_FILTER_PROTO_CHANGE|AP_FILTER_PROTO_CHANGE_LENGTH
static void register_hooks(apr_pool_t *p)
{
    ap_register_output_filter("IMGMIN", imgmin_out_filter,  NULL, AP_FTYPE_RESOURCE);
}

static const command_rec imgmin_filter_cmds[] = {
    AP_INIT_TAKE1("ImgminErrorThreshold",      imgmin_set_error_threshold, NULL, RSRC_CONF, "Set error threshold (0-255.0)"),
    AP_INIT_TAKE1("ImgminCacheDir",            imgmin_set_cache_dir,       NULL, RSRC_CONF, "Cache dir prefix. Default /var/imgmin-cache"),
    AP_INIT_TAKE1("ImgminBufferSize",          imgmin_set_buffer_size,     NULL, RSRC_CONF, "Set maximum buffer size based on largest feasible image"),
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
