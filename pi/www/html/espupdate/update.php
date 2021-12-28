<?PHP
    $fh1 = fopen("lastaccess.sent", 'a');
    $req_dump1 = print_r($_SERVER, TRUE);
    fwrite($fh1, $req_dump1);
    fclose($fh1);

function check_header($name, $value = false) {
    if(!isset($_SERVER[$name])) {
        return false;
    }
    if($value && $_SERVER[$name] != $value) {
        return false;
    }
    return true;
}

function sendFile($path) {
    header($_SERVER["SERVER_PROTOCOL"].' 200 OK', true, 200);
    header('Content-Type: application/octet-stream', true);
    header('Content-Disposition: attachment; filename='.basename($path));
    header('Content-Length: '.filesize($path), true);
    header('x-MD5: '.md5_file($path), true);
    readfile($path);
}

$mac = isset($_SERVER['HTTP_X_ESP8266_STA_MAC'])?$_SERVER['HTTP_X_ESP8266_STA_MAC']:"";
$version = isset($_SERVER['HTTP_X_ESP8266_VERSION'])?$_SERVER['HTTP_X_ESP8266_VERSION']:null;
if ($version) {
  $updateFile = "./bin/".$version;

  if (!file_exists($updateFile.".sent") && file_exists($updateFile)) {
    sendFile($updateFile);

    $fh = fopen($updateFile.".sent", 'a');
    fwrite($fh, 'update sent to '.$mac.".".$version);
    $req_dump = print_r($_SERVER, TRUE);
    fwrite($fh, $req_dump);
    fclose($fh);
  } else
    header($_SERVER["SERVER_PROTOCOL"].' 304 Not Modified', true, 304);
  exit();
}

header($_SERVER["SERVER_PROTOCOL"].' 403 Forbidden', true, 403);
echo "only for ESP8266 updater!\n";

