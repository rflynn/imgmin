<?php

/*
 * web service frontend to imgmin.pl
 */

if ($_POST)
{

    if (!$_FILES || $_FILES['img']['error'])
    {
    
        echo '<pre>';
        foreach(array('file_uploads','upload_tmp_dir','upload_max_filesize','post_max_size') as $k)
            printf("%s:%s\n", $k, ini_get($k));
         
        print_r($_POST);
        print_r($_FILES);
        echo '</pre>';

    } else if (in_array($_FILES['img']['type'], array('image/jpeg', 'image/gif', 'image/png')) {

        $filename = basename($_FILES['img']['name']);
        $src = $_FILES['img']['tmp_name'];
        $dst = '/tmp/' . $filename;

        $cmd = sprintf("../imgmin.pl %s %s",
            escapeshellarg($src), escapeshellarg($dst));

        exec($cmd);
        header('Content-type: image/jpeg');
        header('Content-Disposition: attachment; filename="'.$filename.'"');
        readfile($dst);

    }


} else {
?>

<html>
<body>
<form enctype="multipart/form-data" action="svc.php" method="POST">
<label for="file">Filename:</label>
<input type="hidden" name="MAX_FILE_SIZE" value="1000000" />
<input type="file" name="img" id="file" /> 
<input type="submit" name="submit" value="Submit" />
</form>
</body>
</html>

<?php

}

?>
