
"use strict";

function ReadCookie(name)
{
    const v = RegExp("(?:^|;\\s*)" + name + "=([^;=]*)").exec(document.cookie);
    return v && v[1];
}

function SetCookie(name, value)
{
    document.cookie = name + "=" + value + ";path=/;secure";
}

function DeleteCookie(name)
{
    SetCookie(name, ";expires=Thu, 01 Jan 1970 00:00:01 GMT");
}

function Delay(timeout, Func)
{
    window.setTimeout(Func, timeout);
}

function E(id)
{
    let elem = (typeof id === "string") ? document.getElementById(id) : id;
    return {
        elem: elem,
        show: function() { this.elem.style.display = "block"; },
        hide: function() { this.elem.style.display = "none"; },
        isVisible: function() { return window.getComputedStyle(this.elem).display !== "none"; },
        insert: function(tag, contents, beforeNode) {
            const newElem = document.createElement(tag);
            if (contents) {
                newElem.innerHTML = contents;
            }
            if (beforeNode) {
                return E(this.elem.insertBefore(newElem, beforeNode));
            }
            else {
                return E(this.elem.appendChild(newElem));
            }
        },
        setContents: function(contents) { this.elem.innerHTML = contents; },
/*
        setHeight: function(height) { this.elem.style.height = height; },
        setAttr: function(name, value) {
            const attr = document.createAttribute(name);
            attr.value = value;
            return this.elem.setAttributeNode(attr);
        },
        setClass: function(value) {
            return this.setAttr("class", value);
        },
        setStyle: function(value) {
            return this.setAttr("style", value);
        },
        scrollToBottom: function() {
            this.elem.scrollTop = this.elem.scrollHeight;
        }
*/
    };
}

function ConnectionError(method, url, contents, type, Sent)
{
    // TODO
}

function Send(method, url, contents, type, Sent)
{
    if (false && url === "/sysinfo") {
        Sent({status: 200, responseText: '{"sdk":"2.1.0(7106d38)","heap_free":47328,"uptime_us":93688371,"reset_reason":0,"timestamp":1561838292,"cur_time":"Sat Jun 29 19:58:12 2019","timezone":1,"wifi_mode":1,"ip":"192.168.1.15","mac":"BC:DD:C2:24:C8:19"}'});
        return;
    }

    const request = new XMLHttpRequest();
    request.onreadystatechange = function() {
        if (request.readyState === 4) {
            if (request.status === 0 || request.status === 408) {
                ConnectionError(method, url, contents, type, Sent);
            }
            else {
                Sent(request);
            }
        }
    };
    request.open(method, url, true);
    if (type !== null) {
        request.responseType = type;
    }
    if (contents !== null) {
        request.setRequestHeader("Content-Type", "application/json");
    }
    request.send(contents);
}

function OnPageLoad()
{
    const table = E("sysinfo").insert("table");

    Send("GET", "/sysinfo", null, null, function(request) {
        if (request.status !== 200) return;
        const r = eval("(" + request.responseText + ")");
        const Add = function(name, value) {
            const row = table.insert("tr");
            row.insert("td", name);
            row.insert("td", value);
        };
        Add("Uptime [s]", parseInt(r.uptime_us) / 1000000.0);
        const reasons = ["Power On", "Watchdog", "Exception", "Soft Watchdog", "Software Reset", "", "Reset"];
        Add("Reset Reason", reasons[r.reset_reason]);
        Add("IP", r.ip);
        Add("MAC Address", r.mac);
        Add("Current Time", r.cur_time);
        Add("Timezone", r.timezone);
        if (r.wifi_mode != 1) {
            Add("WiFi Op Mode", r.wifi_mode);
        }
        Add("SDK Version", r.sdk);
        Add("Heap Free [B]", r.heap_free);
    });
}
