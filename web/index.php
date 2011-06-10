<html>
<head>
<style type="text/css">
body { font-family: arial; font-size:small }
.r { display:table-row }
.c { display:table-cell }
.err { font-size:200%; color: red }
.dir { font-family: Courier New, sans-serif }
code { color: green }
</style>
</head>
<body>
<script language="javascript" src="static/js/jquery-1.6.1.js"></script>

<?php

/*
 * Image minimizer gui.
 * 
 * Author: Ryan Flynn <parseerror+imgmin@gmail.com>
 * 
 * Resamples/re-encodes images in popular web formats to reduce size to reduce bandwidth.
 * Relies on
 * 	ImageMagick http://www.imagemagick.org/
 *	pngnq http://pngnq.sourceforge.net/
 */

define('THUMBSIZE', 125);
define('TEMPDIR', './tmp/');
define('TEMPABS', realpath('.') . '/' . TEMPDIR);

// check temp dir setup
if (!file_exists(TEMPDIR))
	tempdir_error('does not exist', true);
else if (!is_writable(TEMPDIR))
	tempdir_error('is not writable by the application');

/*
 * for each image in [original + original.resampled]:
 * 	calculate difference metrics between it and the original image
 *	generate jquery/javascript which adds it to the thumb panel
 */
function displayThumb($orig)
{
	$i = 0;
	$show = array_merge(array($orig), $orig->resampled);
	foreach ($show as $res)
	{
		$pct = round($res->bytes / $orig->bytes * 100);
		$sizediff = $orig->bytes - $res->bytes;
		$cmp = $res->cmp($orig) . '%';
		preg_match('/(\d\.\d\d).(jpe?g|gif|png)$/i', $res->path, $m);
		$target_diff = @$m[1] ? $m[1] : '0';
?>
$('#thumbs').html($('#thumbs').html() +
'<div style="display:table; width:280px; margin-bottom:1px">' +
'	<div id="row<?= $i ?>" class="row" style="display:table-row">' +
'		<div style="display:table-cell; width:<?= THUMBSIZE ?>px"><a href="javascript:showThumb(\'row<?= $i ?>\', \'<?= $res->path ?>\')"><img src="<?= $res->thumbpath ?>" height="<?= $orig->thumbheight ?>" width="<?= $orig->thumbwidth ?>" style="border-width:1px"></a></div>' +
'		<div style="display:table-cell; text-align:left; vertical-align:top; padding:2px; border:1px solid #ccc">' +
'			<?= $target_diff ?><span style="color:#999;font-size:110%">σ</span><br>' +
'			<div style="display:inline-block; margin-right:3px; width:<?= $pct ?>px; height:10px; background-image:url(static/img/progress-bar.png)"></div><?= $pct ?>%<br>' +
'			Size: <?= round($res->bytes / 1024.0, 1) ?> Kb<br>' +
'			Saved: <?= round($sizediff / 1024.0, 1) ?> Kb' +
'		</div>' +
'	</div>' +
'</div>')
<?php
		$i++;
	}
	printf("showThumb('row0', '%s')", $show[0]->path);
}

class Img
{
	public $tmpdir = TEMPDIR;

	function __construct($path, $thumbpx=THUMBSIZE)
	{
		$this->path = $path;
		$this->thumbpx = $thumbpx;
		$this->bytes = filesize($path);
		list($this->width, $this->height, $type, $attr) = getimagesize($path);
		$this->ext = strtolower(substr($path, strrpos($path, '.')+1));
		$thumbfactor = max($this->width / $thumbpx, $this->height / $thumbpx);
		$this->thumbwidth = $this->width / $thumbfactor;
		$this->thumbheight = $this->height / $thumbfactor;
		$this->thumbpath = $this->thumbnail();
		$this->resampled = array();
	}

	function thumbnail()
	{
		$thumbpath = $this->tmpdir . basename($this->path) . '.thumb.' . $this->ext;
		if (!file_exists($thumbpath))
			exec(sprintf('convert -resize %ux%u %s %s',
				$this->thumbpx, $this->thumbpx, escapeshellarg($this->path), escapeshellarg($thumbpath)));
		return $thumbpath;
	}

	function cmp($other)
	{
		$cmp = NAN;
		/*
		 * -metric: algorithm for comparison. SSIM > RMSE, but compare doesn't support it.
		 */
		$sys = shell_exec(sprintf('compare -metric RMSE %s %s /dev/null 2>&1', escapeshellarg($this->path), escapeshellarg($other->path)));
		// extract pct difference
		if (preg_match('/\((.*?)\)/', $sys, $m))
			$cmp = round(floatval($m[1]) * 100, 2);
		return $cmp;
	}

	function resample()
	{
		switch ($this->ext)
		{
		case 'jpeg':
		case 'jpg': $this->resampleJPG(); break;
		case 'gif': $this->resampleGIF(); break;
		case 'png': $this->resamplePNG(); break;
		default:
			break;
		}
	}

	function resampleJPG()
	{
		foreach (array(0.5, 0.75, 1, 1.5, 2, 3) as $pct)
		{
			$newpath = sprintf('%s%s.%4.2f.%s', $this->tmpdir, basename($this->path), $pct, $this->ext);
			if (!file_exists($newpath))
				exec(sprintf('../imgmin.sh %s %s %.2f', escapeshellarg($this->path), escapeshellarg($newpath), $pct));
			$this->resampled[] = new Img($newpath);
		}
	}

	function resampleGIF()
	{
		foreach (array(128, 64, 32, 16, 8) as $colors)
		{
			$newpath = $this->tmpdir . basename($this->path) . '.' . $colors . '.' . $this->ext;
			/*
			 *
			 */
			exec(sprintf('convert -colors %u -strip %s %s', $colors, escapeshellarg($this->path), escapeshellarg($newpath)));
			$this->resampled[] = new Img($newpath);
		}
	}

	function resamplePNG()
	{
		exec(sprintf('pngnq %s', escapeshellarg($this->path)));
		// path dictated by pngnq
		$newpath = substr($this->path, 0, strrpos($this->path, '.')) . '-nq8.png';
		$this->resampled[] = new Img($newpath);
	}

}

function safedir($dir)
{
	$safer = preg_replace('#^/+#', '', # disallow leading /
		preg_replace('#\.{2,}#', '', $dir)); # disallow ..
	return $safer;
}

function list_images($dir)
{
	$files = array();
	$d = opendir($dir);
	while (($f = readdir($d)))
		if (preg_match('/\.(jpe?g|gif|png)$/i', $f))
			$files[] = "$dir/$f";
	closedir($d);
	return $files;
}

$DIR = @$_GET['dir'] ? safedir($_GET['dir']) : 'images';
$IMG = @$_GET['img'] ? $_GET['img'] : '1hulk-554x398.jpg';
$old = new Img($IMG);

?>

<div>
<?php foreach (list_images($DIR) as $f) {
	$img = new Img($f);
?>
	<div style="display:inline-block"><a href="<?= $PHP_SELF ?>?dir=<?= $DIR ?>&img=<?= $img->path ?>"><img src="<?= $img->thumbpath ?>" width="<?= $img->thumbwidth ?>" height="<?= $img->thumbheight ?>" style="border-width:1px"></a></div>
<?php }
ob_flush();
?>
</div>

<div style="display:table">
	<div class="r">
		<div class="c"><em>Original</em></div>
		<div class="c"><em>Resampled</em></div>
		<div class="c"><em>Sample Gallery</em></div>
	</div>
	<div class="r">
		<div class="c">
			<div style="background-image:url(static/img/bg.png)" >
				<img id="oldimg" src="<?= htmlentities($old->path) ?>" width="<?= $old->width ?>" height="<?= $old->height ?>">
			</div>
		</div>
		<div class="c">
			<div style="background-image:url(static/img/bg.png)" >
				<img id="newoldbg" src="<?php echo htmlentities($old->path) ?>" style="position:absolute; z-index:0" width="<?= $old->width ?>" height="<?= $old->height ?>">
				<img id="newimg" src="" width="<?= $old->width ?>" height="<?= $old->height ?>" style="z-index:1">
			</div>
		</div>
		<div id="thumbs" class="c" style="vertical-align:top; padding-left:1px"></div>
	</div>
</div>

<script language="javascript">

function showThumb(a, path)
{

	$('#newoldbg').show()
	$('#newimg').hide()
	$('#newimg').attr('src', path)
	$('#newimg').fadeIn('fast', function()
	{
		$('#newoldbg').hide()
	})
	$('.row').css('background-color', '#fff')
	$('#' + a).css('background-color', '#eee')
}

$(document).ready(function() {

<?php
$old->resample();
displayThumb($old);
?>


})

</script>
</body>
</html>

<?php

function www_user_and_group()
{
	$www_user = `whoami`; # FIXME: naughty naughty
	$www_groups = explode(' ', trim(`groups`)); # FIXME: naughty naughty
	$www_group = $www_groups[0];
	return array($www_user, $www_group);
}

function show_cmds($mkdir=false)
{
	list($www_user, $www_group) = www_user_and_group();
?>
	<div>
<?php
	if ($mkdir) {
?>
		<div><code>sudo mkdir <?= TEMPABS ?> # create directory</code></div>
<?php
	}
?>
		<div><code>sudo chgrp <?= $www_group ?> <?= TEMPABS ?> # change group</code></div>
		<div><code>sudo chmod g+w <?= $www_group ?> <?= TEMPABS ?> # grant writes to group</code></div>
	</div>
<?php
}

function tempdir_error($msg, $mkdir=false)
{
?>
	<div class="err">
		<div>Tempdir <span class="dir"><?= TEMPDIR ?></span> <?= $msg ?>.</div>
		<?php show_cmds($mkdir); ?>
	</div>
<?php
}

?>
