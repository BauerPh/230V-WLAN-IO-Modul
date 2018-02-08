<?PHP
header('Content-Type: text/plain; charset=utf8', true);

$db = include("version.php");

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
    header($_SERVER["SERVER_PROTOCOL"].' 200 OK',true, 200);
    header('Content-Type: application/octet-stream');
    header('Content-Disposition: attachment; filename='.basename($path));
	header('Content-Length: '.filesize($path));
    header('x-esp8266-updateSize: '.filesize($path));
    header('x-esp8266-MD5: '.md5_file($path));
    readfile($path);
}

//Check User Agent
if(!check_header('HTTP_USER_AGENT', 'ESP8266-http-Update')) {
    header($_SERVER["SERVER_PROTOCOL"].' 403 Forbidden', true, 403);
    echo "only for ESP8266 updater!\n";
    exit();
}

//Check if new Firmware is available
if (check_header('HTTP_X_ESP8266_CHECKUPDATE')) {
	if (check_header('HTTP_X_ESP8266_MODEL') && check_header('HTTP_X_ESP8266_VERSION')) {
		//Check for Update
		if(isset($db[$_SERVER['HTTP_X_ESP8266_MODEL']])) {
			$path = "bin/".$_SERVER['HTTP_X_ESP8266_MODEL'].".bin";
			if ($db[$_SERVER['HTTP_X_ESP8266_MODEL']] != $_SERVER['HTTP_X_ESP8266_VERSION']) { 
				header('x-esp8266-updateAvailable: ');
			}
			header('x-esp8266-serverVersion: '.$db[$_SERVER['HTTP_X_ESP8266_MODEL']]);
			header('x-esp8266-clientVersion: '.$_SERVER['HTTP_X_ESP8266_VERSION']);
			header('x-esp8266-updateSize: '.filesize($path));
			header($_SERVER["SERVER_PROTOCOL"].' 200 OK', true, 200);
		} else {
			header($_SERVER["SERVER_PROTOCOL"].' 416 no version for this Model', true, 416);
		}
	} else {
			header($_SERVER["SERVER_PROTOCOL"].' 400 bad request', true, 400);
	}
	exit();
}

//Check Headers for Update
if (!check_header('HTTP_X_ESP8266_MODEL') || !check_header('HTTP_X_ESP8266_VERSION')) {
    header($_SERVER["SERVER_PROTOCOL"].' 400 bad request', true, 400);
    exit();
}

//Send SPIFFS or Firmware
if(isset($db[$_SERVER['HTTP_X_ESP8266_MODEL']])) {
	if (check_header('HTTP_X_ESP8266_SPIFFS')) {
		$path = "bin/".$_SERVER['HTTP_X_ESP8266_MODEL']."_SPIFFS.bin";
		header('x-esp8266-SPIFFS: ');
	} else {
		$path = "bin/".$_SERVER['HTTP_X_ESP8266_MODEL'].".bin";
	}
    if ($db[$_SERVER['HTTP_X_ESP8266_MODEL']] != $_SERVER['HTTP_X_ESP8266_VERSION']) { 
		sendFile($path);
    } else {
        header($_SERVER["SERVER_PROTOCOL"].' 304 Not Modified', true, 304);
    }
    exit();
} else {
	header($_SERVER["SERVER_PROTOCOL"].' 416 no version for this Model', true, 416);
}

?>