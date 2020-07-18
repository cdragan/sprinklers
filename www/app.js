
"use strict";

let enzones = [1,1,1,1,1,1]; // TODO change to all zeroes, load with sysinfo
let zones = null;

function SetZone(zone, en)
{
    zones = enzones.map(function(x) { return x ? 0 : -1; });
    if (en) zones[zone - 1] = 1;
    for (let i = 1; i <= zones.length; i++)
        E("z" + i).setContents(i == zone && en ? "Disable" : "Enable");
}

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

function DelayMs(timeoutMs, Func)
{
    window.setTimeout(Func, timeoutMs);
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
        setAttr: function(name, value) {
            const attr = document.createAttribute(name);
            attr.value = value;
            return this.elem.setAttributeNode(attr);
        },
        setClass: function(value) {
            return this.setAttr("class", value);
        },
        addButton: function(id, name, func) {
            const btn = this.insert("a", name);
            btn.setClass("button box shadow");
            btn.setAttr("href", "#");
            btn.setAttr("id", id);
            btn.elem.onclick = func;
        },
/*
        setHeight: function(height) { this.elem.style.height = height; },
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

// TODO remove this
function Simulate(url, contents)
{
    if (url === "/sysinfo") {
        return {status: 200, responseText: '{"sdk":"2.1.0(7106d38)","heap_free":47328,"uptime_us":93688371,"reset_reason":0,"timestamp":1561838292,"cur_time":"Sat Jun 29 19:58:12 2019","timezone":1,"wifi_mode":1,"ip":"192.168.1.15","mac":"BC:DD:C2:24:C8:19"}'};
    }
    else if (url.startsWith("/manual?")) {
        return {status: 200};
    }
    return {status: 404};
}

function Send(method, url, contents, type, Sent, retryCount)
{
    retryCount = retryCount || 0;

    // TODO remove this
    if (/^file:/.test(window.location.href)) {
        DelayMs(100, function() {
            console.log(url);
            Sent(Simulate(url, contents));
        });
        return;
    }

    const request = new XMLHttpRequest();
    request.onreadystatechange = function() {
        if (request.readyState === 4) {
            if (request.status === 200)
                Sent(request);
            else if (retryCount < 10) {
                DelayMs(1000, function() {
                    Send(method, url, contents, type, Sent, retryCount + 1);
                });
            }
            else
                ConnectionError(method, url, contents, type, Sent);
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
    E("quickbar").addButton("togauto", "Start Full Cycle", ToggleAuto);
    for (let iz = 0; iz < 6; iz++) {
        const z = E("z" + (iz + 1));
        const m = zone_map[iz];
        const e = z.insert("span", m.name);
        const b = z.addButton("togz" + iz, "Start");
    }

    SetZone();

    const table = E("sysinfo").insert("table");

    Send("GET", "/sysinfo", null, null, function(request) {
        if (request.status !== 200) return;
        const r = eval("(" + request.responseText + ")");
        const Add = function(name, value) {
            const row = table.insert("tr");
            row.insert("td", name);
            row.insert("td", value);
        };
        const reasons = ["Power On", "Watchdog", "Exception", "Soft Watchdog", "Software Reset", "", "Reset"];
        const ridx = r.reset_reason;
        Add("Reset Reason", ridx >= 0 && ridx < reasons.length ? reasons[r.reset_reason] : "" + ridx);
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

/*
 * - Timer, every 30s query status from device
 *
 * - Button [Start full cycle] (automatic)
 *   * Interrupts any manual cycle
 *
 * - For each zone, tile with:
 *   * Name of the zone (grayed out "zone #")
 *   * Button (for zones which are not set up: just a box)
 *   * Time it takes to water the zone
 *   * Button to enable or disable a zone
 *
 * - Button [Configure]
 *   * Rename zones
 *   * Change zone times
 *   * Reorder zones (up or down)
 *
 * During a full/automatic cycle, started with button or started automatically:
 *   - Info that watering is going on
 *   - Current zone buttons: [Skip]
 *   - Previous zones grayed out (not clickable)
 *   - Next zones colored to indicate they are ready (not clickable)
 *   - Show elapsed/remaining time for each zone
 *
 * In manual mode (if automatic cycle not running):
 *   - Manual button for each zone: [Start] or [Stop]
 *   - Show [Configure] button
 *   - In manual mode, if a zone is started, it will automatically
 *     turn off when its normal configured time runs out.
 *
 */

// Checks whether the zone number is correct
function ZoneOK(zone)
{
    return typeof zone === "number" &&
           zone >= 1 && zone <= 6;
}

// Checks whether the zone number is correct and whether that zone is enabled
function ZoneEnabled(zone)
{
    return ZoneOK(zone) && zones[zone - 1] >= 0;
}

// Sets local zone state and updates UI
// 0 means no zone enabled, 1-6 means enable only that zone
function SetZone(id)
{
    for (let i = 0; i < zones.length; i++) {
        if (zones[i] >= 0)
            zones[i] = (i == id - 1) ? 1 : 0;
        // TODO update UI
    }
}

// Stop zone on the device
function StopZone(zone)
{
    SetZone(0);

    Send("PUT", "/manual", "{\"zone\":" + zone + ",\"state\":0}", null, function(request) {
        if (request.status === 200) {
            console.log(zone + " " + state);
            SetZone(zone, state);
        }
        else {
            // TODO report error
        }
    });
}

// Starts automatic watering cycle
function ToggleAuto()
{
    StopZone(0);
    // 1. Stop any manual zones
    // 2. Send trigger to device
    // 3. Update UI
}

// Starts zone configuration
function ConfigZones()
{
}

// Starts, stops or skips (depending on mode/state)
function ZoneAction(zone)
{
}

// Enables or disables a zone
function ToggleZone(zone)
{
}

function ToggleZoneOLD(zone)
{
    if (!ZoneEnabled(zone)) return;

    const state = zones[zone - 1] ? 0 : 1;

    Send("PUT", "/manual", "{\"zone\":" + zone + ",\"state\":" + state + "}", null, function(request) {
        if (request.status === 200) {
            console.log(zone + " " + state);
            SetZone(zone, state);
        }
        else {
            // TODO report error
        }
    });
}
