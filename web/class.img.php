<?php

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
		{
			$cmd = sprintf('%sconvert -resize %ux%u %s %s 2>&1',
				IMAGEMAGICKPATH, $this->thumbpx, $this->thumbpx, escapeshellarg($this->path), escapeshellarg($thumbpath));
			#echo "<pre>$cmd</pre>";
			exec($cmd, &$out, &$err);
			#echo '<pre>'.print_r($out,1).'</pre>';
			#print_r($err);
		}
		return $thumbpath;
	}

	function cmp($other)
	{
		$cmp = NAN;
		/*
		 * -metric: algorithm for comparison. SSIM > RMSE, but compare doesn't support it.
		 */
		$cmd = sprintf('%scompare -metric RMSE %s %s /dev/null 2>&1',
			IMAGEMAGICKPATH, escapeshellarg($this->path), escapeshellarg($other->path));
		#echo "<pre>$cmd</pre>";
		$sys = shell_exec($cmd);
		#echo "<pre>$sys</pre>";
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
		foreach (array(0.5, 0.75, 1, 1.5, 2, 3, 4, 10) as $pct)
		{
			$newpath = sprintf('%s%s.%4.2f.%s', $this->tmpdir, basename($this->path), $pct, $this->ext);
			if (!file_exists($newpath))
			{
				$cmd = sprintf('%simgmin.sh %s %s %.2f', IMGMINPATH, escapeshellarg($this->path), escapeshellarg($newpath), $pct);
				#echo "<pre>$cmd</pre>";
				exec($cmd, &$out);
				#echo '<pre>'.print_r($out,1).'</pre>';
			}
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

?>
