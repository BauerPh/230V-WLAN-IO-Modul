function testStr(elementId, type, rangeFrom, rangeTo) {
    var tString = document.getElementById(elementId).value;
    var okay = false;
    switch (type) {
        case "ip":
            okay = (tString.match(/^(?:(?:[1-9]?[0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}(?:[1-9]?[0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$/g) ? true : false);
            break;
        case "string":
            okay = ((tString.match(/^[a-z0-9._\/:-]+$/gi) ? true : false) && tString.length >= rangeFrom && tString.length <= rangeTo);
            break;
        case "mqtt_topic":
            okay = ((tString.match(/^[a-z0-9_-][a-z0-9_\/-]+[a-z0-9_-]$/gi) ? true : false) && tString.length >= rangeFrom && tString.length <= rangeTo);
            break;
        case "number":
            okay = ((tString.match(/^[0-9]+$/g) ? true : false) && parseInt(tString, 10) >= rangeFrom && parseInt(tString, 10) <= rangeTo);
            break;
        case "ssid":
            okay = ((tString.match(/^([^!#;+\[\]/"\s])[^+\[\]/"\s]*$/gi) ? true : false) && tString.length >= rangeFrom && tString.length <= rangeTo);
            break;
        default:
            okay = (tString.match(/[a-z0-9.,;:öäüß _-]+/gi) ? true : false);
            break;
    }
    if (!okay) document.getElementById(elementId).setAttribute("style", "background: #FFA07A");
    return okay;
}

function resetBg(elementId) { document.getElementById(elementId).setAttribute("style", "background: #FFFFFF"); }
function resetBgThis() { this.setAttribute("style", "background: #FFFFFF"); }

function microAjax(B, A) {
    this.bindFunction = function (E, D) {
        return function () {
            return E.apply(D, [D]);
        };
    };
    this.stateChange = function (D) {
        if (this.request.readyState == 4) {
            this.callbackFunction(this.request.responseText);
        }
    };
    this.getRequest = function () {
        if (window.ActiveXObject) {
            return new ActiveXObject("Microsoft.XMLHTTP");
        } else {
            if (window.XMLHttpRequest) {
                return new XMLHttpRequest();
            }
        } return false;
    };
    this.postBody = (arguments[2] || "");
    this.callbackFunction = A;
    this.url = B;
    this.request = this.getRequest();
    if (this.request) {
        var C = this.request;
        C.onreadystatechange = this.bindFunction(this.stateChange, this);
        if (this.postBody !== "") {
            C.open("POST", B, true);
            C.setRequestHeader("X-Requested-With", "XMLHttpRequest");
            C.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
        } else {
            C.open("GET", B, true);
        }
        C.send(this.postBody);
    }
}

function setValues(url, callback) {
    microAjax(url, function (res) {
        res.split(String.fromCharCode(10)).forEach(
            function (entry) {
                fields = entry.split("|");
                if (fields[2] == "input") {
                    document.getElementById(fields[0]).value = fields[1];
                }
                else if (fields[2] == "div") {
                    document.getElementById(fields[0]).innerHTML = fields[1];
                } else if (fields[2] == "chk") {
                    document.getElementById(fields[0]).checked = fields[1];
                }
            }
        );
        if (callback) callback();
    });
}
