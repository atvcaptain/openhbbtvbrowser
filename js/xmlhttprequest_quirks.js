window.cefXmlHttpRequestQuirk = function(uri) {
  // Start 1.
  //    Das Erste + ...

  // The URL contains sid as decimal value, but hex value is needed
  let sid = /(getapp\.php\?sid=)(.*?)(&colid=.*?&query=appid)/.exec(uri);

  if (sid) {
      // change sid[2] to hex
      sid[2] = Number(sid[2]).toString(16);
      return sid[1] + sid[2] + sid[3];
  }
  // End 1.


  // return unchanged URL
  return uri;
};

(function() {
  if (window.__openhbbtvAuthHttpDebugInstalled)
    return;
  window.__openhbbtvAuthHttpDebugInstalled = true;

  function authDebugEnabled() {
    return window.OPENHBBTV_AUTH_HTTP_DEBUG === true;
  }

  function hbbtvDebugEnabled() {
    return window.OPENHBBTV_HBBTV_HTTP_DEBUG === true;
  }

  function hbbtvBodyDebugEnabled() {
    return window.OPENHBBTV_HBBTV_HTTP_BODY_DEBUG === true;
  }

  function zdfConsoleDebugEnabled() {
    return window.OPENHBBTV_ZDF_CONSOLE_DEBUG === true;
  }

  function zdfConsoleQueryEnabled() {
    return window.OPENHBBTV_ZDF_CONSOLE_QUERY === true || zdfConsoleDebugEnabled();
  }

  function zdfBootDebugEnabled() {
    return window.OPENHBBTV_ZDF_BOOT_DEBUG === true;
  }

  function zdfBootTraceEnabled() {
    return window.OPENHBBTV_ZDF_BOOT_TRACE === true;
  }

  function zdfSafeInitFetchEnabled() {
    return window.OPENHBBTV_ZDF_SAFE_INIT_FETCH === true;
  }

  function zdfDeepProbeEnabled() {
    return window.OPENHBBTV_ZDF_DEEP_PROBE === true;
  }

  function zdfInitDetailDebugEnabled() {
    return zdfDeepProbeEnabled() || window.OPENHBBTV_ZDF_INIT_DETAIL_DEBUG === true;
  }

  function jsHttpWrapDebugEnabled() {
    return window.OPENHBBTV_JS_HTTP_WRAP_DEBUG === true ||
           authDebugEnabled() ||
           hbbtvBodyDebugEnabled() ||
           zdfDeepProbeEnabled() ||
           zdfInitDetailDebugEnabled();
  }

  function isZdfPage() {
    try {
      var host = String((window.location && window.location.hostname) || "").toLowerCase();
      return host === "new-hbbtv.zdf.de" || host === "hbbtv.zdf.de" || /\.zdf\.de$/.test(host);
    } catch (ignore) {
      return false;
    }
  }

  function zdfStableObjectEntriesEnabled() {
    return window.OPENHBBTV_ZDF_STABLE_OBJECT_ENTRIES !== false;
  }

  function installZdfStableObjectEntries() {
    if (!zdfStableObjectEntriesEnabled() || !isZdfPage() || Object.__openhbbtvZdfStableObjectEntries)
      return;

    try {
      var objectKeys = Object.keys;

      function stableEntries(value) {
        if (value === null || value === undefined)
          throw new TypeError("Cannot convert undefined or null to object");
        var object = Object(value);
        var keys = objectKeys(object);
        var result = [];
        for (var i = 0; i < keys.length; i++) {
          var key = keys[i];
          result.push([key, object[key]]);
        }
        return result;
      }

      function assignEntry(target, entry) {
        if (entry === null || entry === undefined)
          throw new TypeError("Iterator value " + entry + " is not an entry object");
        if (typeof entry !== "object" && typeof entry !== "function")
          throw new TypeError("Iterator value " + entry + " is not an entry object");
        var pair = Object(entry);
        target[pair[0]] = pair[1];
      }

      function stableFromEntries(iterable) {
        if (iterable === null || iterable === undefined)
          throw new TypeError("undefined is not iterable");

        var result = {};
        var index = 0;
        var length = Number(iterable.length);
        if (length === length && length >= 0 && !iterable.next) {
          for (; index < length; index++) {
            if (index in Object(iterable))
              assignEntry(result, iterable[index]);
          }
          return result;
        }

        if (typeof Symbol !== "undefined" && iterable[Symbol.iterator]) {
          var iterator = iterable[Symbol.iterator]();
          var step;
          while (!(step = iterator.next()).done)
            assignEntry(result, step.value);
          return result;
        }

        throw new TypeError("Object.fromEntries() requires a single iterable argument");
      }

      stableEntries.__openhbbtvZdfStableObjectEntries = true;
      stableFromEntries.__openhbbtvZdfStableObjectEntries = true;

      function install() {
        try {
          if (Object.entries !== stableEntries &&
              !(Object.entries && Object.entries.__openhbbtvZdfStableObjectEntries)) {
            Object.defineProperty(Object, "entries", {
              configurable: true,
              writable: true,
              value: stableEntries
            });
          }
          if (Object.fromEntries !== stableFromEntries &&
              !(Object.fromEntries && Object.fromEntries.__openhbbtvZdfStableObjectEntries)) {
            Object.defineProperty(Object, "fromEntries", {
              configurable: true,
              writable: true,
              value: stableFromEntries
            });
          }
        } catch (error) {
          if (zdfBootDebugEnabled())
            log("ZDFBOOT", "stable Object.entries install failed " + stackText(error));
        }
      }

      install();
      setTimeout(install, 0);
      setTimeout(install, 50);
      setTimeout(install, 250);
      setTimeout(install, 1000);

      Object.__openhbbtvZdfStableObjectEntries = true;
      if (zdfBootDebugEnabled())
        log("ZDFBOOT", "stable Object.entries/fromEntries installed");
    } catch (error) {
      if (zdfBootDebugEnabled())
        log("ZDFBOOT", "stable Object.entries setup failed " + stackText(error));
    }
  }

  function shouldLogAuth(url) {
    url = String(url || "");
    return url.indexOf("accounts.ard.de/device/") >= 0 ||
           url.indexOf("accounts.ard.de/id") >= 0 ||
           url.indexOf("tv.ardmediathek.de/dyn/get?id=login") >= 0;
  }

  function shouldLogHbbtv(url) {
    url = String(url || "");
    var lower = url.toLowerCase();
    return lower.indexOf("/al/init") >= 0 ||
           lower.indexOf("/ds/configuration") >= 0 ||
           lower.indexOf("/configuration/") >= 0 ||
           lower.indexOf("/oipf/uploadxml") >= 0 ||
           lower.indexOf("/data/menuitems.json") >= 0 ||
           (lower.indexOf("hbbtv") >= 0 &&
            (lower.indexOf("/config") >= 0 || lower.indexOf("configuration") >= 0 || lower.indexOf("/init") >= 0));
  }

  function labelsFor(url) {
    var labels = [];
    if (authDebugEnabled() && shouldLogAuth(url))
      labels.push("AUTHHTTP");
    if (hbbtvDebugEnabled() && shouldLogHbbtv(url))
      labels.push("HBBTVHTTP");
    return labels;
  }

  function mask(text) {
    text = String(text || "");
    text = text.replace(/(accounts\.ard\.de\/device\/token\/)([A-Z0-9]{5})\+[^"'\s<>)&]+/g, "$1$2+***");
    text = text.replace(/("token_uri"\s*:\s*"https:\/\/accounts\.ard\.de\/device\/token\/)([A-Z0-9]{5})\+[^"]+(")/g, "$1$2+***$3");
    text = text.replace(/("(?:access_token|refresh_token|id_token|accessToken|refreshToken|idToken|device_secret|deviceSecret|bearerToken|token|auth|authorization|session|sessionId|userId|userid)"\s*:\s*")[^"]+(")/gi, "$1***$2");
    text = text.replace(/([?&](?:token|auth|authorization|session|sessionId|userId|userid|key)=)[^&\s"'<>)]+/gi, "$1***");
    return text.length > 420 ? text.slice(0, 420) + "..." : text;
  }

  function stackText(error) {
    try {
      return String((error && error.stack) || error || "").replace(/\s+/g, " ").slice(0, 260);
    } catch (ignore) {
      return "[stack-unreadable]";
    }
  }

  function describeValue(value) {
    try {
      if (value === undefined)
        return "undefined";
      if (value === null)
        return "null";
      if (Array.isArray(value))
        return "array(" + value.length + ")";
      if (typeof value === "object")
        return "object{" + Object.keys(value).slice(0, 8).join("|") + "}";
      if (typeof value === "string")
        return "string(" + value.length + ") " + value.slice(0, 80).replace(/\s+/g, " ");
      return typeof value + "(" + String(value) + ")";
    } catch (ignore) {
      return Object.prototype.toString.call(value);
    }
  }

  function zdfTrace(label, text) {
    try {
      if (!zdfBootTraceEnabled() || !isZdfPage() || !window.console)
        return;
      var message = "[OpenHbbTV][ZDFTRACE] " + label + (text ? " " + mask(text) : "");
      if (typeof window.console.warn === "function")
        window.console.warn(message);
      else if (typeof window.console.log === "function")
        window.console.log(message);
    } catch (ignore) {}
  }

  function zdfTraceErrorWanted(error) {
    try {
      var text = stackText(error);
      return text.indexOf("Cannot read property '0'") >= 0 ||
             text.indexOf("Cannot read properties of undefined") >= 0 ||
             text.indexOf("reading '0'") >= 0 ||
             text.indexOf("appConfigLoader getInit") >= 0;
    } catch (ignore) {
      return false;
    }
  }

  function bodyText(body) {
    try {
      if (body === undefined || body === null)
        return "";
      if (typeof body === "string")
        return body;
      if (typeof URLSearchParams !== "undefined" && body instanceof URLSearchParams)
        return body.toString();
      if (typeof FormData !== "undefined" && body instanceof FormData) {
        var parts = [];
        body.forEach(function(value, key) {
          parts.push(key + "=" + (typeof value === "string" ? value : "[blob]"));
        });
        return parts.join("&");
      }
      if (typeof body === "object" && body.toString && body.toString !== Object.prototype.toString)
        return body.toString();
      return JSON.stringify(body);
    } catch (ignore) {
      return "[unreadable]";
    }
  }

  function logPieces(label, prefix, text) {
    text = String(text || "");
    if (!text) {
      log(label, prefix);
      return;
    }
    for (var pos = 0; pos < text.length; pos += 300)
      log(label, prefix + " " + text.slice(pos, pos + 300));
  }

  function logZdfInitDetails(url, body) {
    try {
      if (!zdfInitDetailDebugEnabled() || !isZdfInitUrl(url))
        return;
      var value = JSON.parse(String(body || ""));
      var dataObj = value && value.data && typeof value.data === "object" ? value.data : null;
      if (!dataObj)
        return;
      var keys = Object.keys(dataObj);
      var typed = [];
      for (var i = 0; i < keys.length; i++)
        typed.push(keys[i] + "=" + describeValue(dataObj[keys[i]]));
      logPieces("ZDFINIT", "keys", keys.join("|"));
      logPieces("ZDFINIT", "types", typed.join(" "));
      log("ZDFINIT", "runtime search=" + String(window.location && window.location.search || "") +
          " entriesProbe=" + !!(Object.entries && Object.entries.__openhbbtvZdfProbe) +
          " fromEntriesProbe=" + !!(Object.fromEntries && Object.fromEntries.__openhbbtvZdfProbe) +
          " mapProbe=" + !!(Array.prototype.map && Array.prototype.map.__openhbbtvZdfProbe) +
          " deepProbe=" + zdfDeepProbeEnabled() +
          " allSettled=" + (window.Promise && Promise.allSettled ? typeof Promise.allSettled : "missing"));
      if (dataObj.texts && typeof dataObj.texts === "object")
        logPieces("ZDFINIT", "texts", Object.keys(dataObj.texts).join("|"));
      if (dataObj.menuItems && dataObj.menuItems.length)
        log("ZDFINIT", "menuFirst " + describeValue(dataObj.menuItems[0]));
      if (dataObj.tracking !== undefined)
        log("ZDFINIT", "tracking " + describeValue(dataObj.tracking));
    } catch (error) {
      log("ZDFINIT", "detail failed " + stackText(error));
    }
  }

  function isZdfInitUrl(url) {
    try {
      return isZdfPage() &&
             String(url || "").toLowerCase().indexOf("/al/init") >= 0;
    } catch (ignore) {
      return false;
    }
  }

  function shouldReadResponseBody(labels, url) {
    try {
      if (!labels || !labels.length)
        return false;
      for (var i = 0; i < labels.length; i++) {
        if (labels[i] === "AUTHHTTP")
          return true;
      }
      return hbbtvBodyDebugEnabled() || (zdfInitDetailDebugEnabled() && isZdfInitUrl(url));
    } catch (ignore) {
      return false;
    }
  }

  function installZdfResponseTextProbe(url, response) {
    try {
      if (!zdfDeepProbeEnabled() || !isZdfInitUrl(url) || !response || typeof response.text !== "function" ||
          response.__openhbbtvZdfTextProbe)
        return;
      var originalText = response.text;
      response.__openhbbtvZdfTextProbe = true;
      response.text = function() {
        log("ZDFBOOT", "Response.text called " + String(url || "") +
            " status=" + response.status + " ok=" + response.ok);
        try {
          return originalText.apply(response, arguments).then(function(body) {
            log("ZDFBOOT", "Response.text resolved " + String(url || "") +
                " len=" + String(body || "").length);
            logZdfInitDetails(url, body);
            return body;
          }, function(error) {
            log("ZDFBOOT", "Response.text rejected " + String(url || "") +
                " error=" + stackText(error));
            throw error;
          });
        } catch (error) {
          log("ZDFBOOT", "Response.text threw " + String(url || "") +
              " error=" + stackText(error));
          throw error;
        }
      };
    } catch (error) {
      log("ZDFBOOT", "Response.text probe failed " + stackText(error));
    }
  }

  function summarizeJson(text) {
    try {
      var value = JSON.parse(String(text || ""));
      var root = value && typeof value === "object" ? Object.keys(value).slice(0, 20).join(",") : typeof value;
      var dataObj = value && value.data && typeof value.data === "object" ? value.data : null;
      var data = dataObj ? Object.keys(dataObj).slice(0, 15).join(",") : "";
      var errors = value && value.errors && value.errors.length !== undefined ? String(value.errors.length) : "";
      var details = [];
      if (dataObj) {
        if (dataObj.menuItems !== undefined)
          details.push("menuItemsLen=" + (dataObj.menuItems && dataObj.menuItems.length !== undefined ? dataObj.menuItems.length : typeof dataObj.menuItems));
        if (dataObj.texts !== undefined)
          details.push("textsKeys=" + (dataObj.texts && typeof dataObj.texts === "object" ? Object.keys(dataObj.texts).slice(0, 10).join("|") : typeof dataObj.texts));
        if (dataObj.tracking !== undefined)
          details.push("trackingKeys=" + (dataObj.tracking && typeof dataObj.tracking === "object" ? Object.keys(dataObj.tracking).join("|") : typeof dataObj.tracking));
        if (dataObj.profileModal !== undefined)
          details.push("profileModalType=" + typeof dataObj.profileModal);
        if (dataObj.fsk !== undefined)
          details.push("fskType=" + typeof dataObj.fsk);
      }
      if (value && value.oipfKnown !== undefined)
        details.push("oipfKnown=" + value.oipfKnown);
      if (value && value.dash !== undefined)
        details.push("dash=" + value.dash);
      if (value && value.dashJsInHbbtv !== undefined)
        details.push("dashJsInHbbtv=" + value.dashJsInHbbtv);
      if (value && value.dashVod !== undefined)
        details.push("dashVod=" + value.dashVod);
      if (value && value.liveRestart !== undefined)
        details.push("liveRestart=" + value.liveRestart);
      return " jsonRoot=" + root + (errors ? " errors=" + errors : "") + (details.length ? " " + details.join(" ") : "") + (data ? " dataKeys=" + data : "");
    } catch (ignore) {
      return "";
    }
  }

  function responseDetail(label, url, body) {
    var detail = summarizeJson(body);
    if (label === "AUTHHTTP" || hbbtvBodyDebugEnabled())
      detail += " body=" + body;
    return detail;
  }

  function log(label, message) {
    try {
      var command = "LOG:" + label + " " + mask(message);
      if (window.signalopenhbbtvbrowser) {
        window.signalopenhbbtvbrowser(command);
      } else if (document && document.title !== undefined) {
        window.__openhbbtvEarlyLogSeq = (window.__openhbbtvEarlyLogSeq || 0) + 1;
        document.title = "OPENATV_HBBTV:" + command + "||early" + window.__openhbbtvEarlyLogSeq;
      }
    } catch (ignore) {}
  }

  function installEarlyVideoBroadcastShim() {
    try {
      if (!window.HTMLObjectElement || !HTMLObjectElement.prototype)
        return;

      var proto = HTMLObjectElement.prototype;
      if (proto.__openhbbtvEarlyVideoBroadcastShim)
        return;

      function send(command) {
        try {
          if (window.signalopenhbbtvbrowser)
            window.signalopenhbbtvbrowser(command);
          else if (document && document.title !== undefined) {
            window.__openhbbtvEarlyLogSeq = (window.__openhbbtvEarlyLogSeq || 0) + 1;
            document.title = "OPENATV_HBBTV:" + command + "||early" + window.__openhbbtvEarlyLogSeq;
          }
        } catch (ignore) {}
      }

      function earlyBroadcastDebugEnabled() {
        return hbbtvDebugEnabled() || zdfBootDebugEnabled() || window.OPENHBBTV_API_AUDIT_DEBUG === true;
      }

      function namespace() {
        window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {};
        return window.HBBTV_POLYFILL_NS;
      }

      function currentChannel() {
        var ns = namespace();
        ns.currentChannel = ns.currentChannel || {
          TYPE_TV: 12,
          channelType: 12,
          onid: 1,
          tsid: 1,
          sid: 1,
          ccid: "ccid:dvbt.1.1.1",
          name: "",
          dsd: ""
        };
        ns.currentChannel = normaliseChannel(ns.currentChannel);
        return ns.currentChannel;
      }

      function normaliseChannel(channel) {
        channel = channel || {};
        function numberOrDefault(value, fallback) {
          var number = Number(value);
          return value === undefined || value === null || value === "" || number !== number ? fallback : number;
        }
        var onid = numberOrDefault(channel.onid, 1);
        var tsid = numberOrDefault(channel.tsid, 1);
        var sid = numberOrDefault(channel.sid, 1);
        channel.TYPE_TV = channel.TYPE_TV || 12;
        channel.channelType = channel.channelType || 12;
        channel.onid = onid;
        channel.tsid = tsid;
        channel.sid = sid;
        channel.ccid = channel.ccid || ("ccid:dvbt." + onid + "." + tsid + "." + sid);
        channel.name = channel.name || "";
        channel.dsd = channel.dsd || "";
        return channel;
      }

      function makeCollection(items) {
        items = items || [];
        items.item = function(index) {
          return index >= 0 && index < this.length ? this[index] : null;
        };
        return items;
      }

      function makeComponent(type) {
        var component = {
          COMPONENT_TYPE_VIDEO: 0,
          COMPONENT_TYPE_AUDIO: 1,
          COMPONENT_TYPE_SUBTITLE: 2,
          componentTag: 0,
          pid: undefined,
          type: type,
          encrypted: false
        };
        if (type === 0) {
          component.aspectRatio = 1.78;
        } else if (type === 1) {
          component.language = "eng";
          component.audioDescription = false;
          component.audioChannels = 2;
        } else if (type === 2) {
          component.language = "deu";
          component.hearingImpaired = false;
          component.encoding = "DVB-SUBT";
        }
        return component;
      }

      function componentsFor(type) {
        if (type === undefined || type === null)
          return makeCollection([makeComponent(0), makeComponent(1), makeComponent(2)]);
        type = Number(type);
        if (type === 0 || type === 1 || type === 2)
          return makeCollection([makeComponent(type)]);
        return makeCollection([]);
      }

      function channelConfig() {
        window.oipf = window.oipf || {};
        var channel = currentChannel();
        var list = makeCollection([channel]);
        list.getChannel = function(ccid) {
          return currentChannel().ccid === ccid ? currentChannel() : null;
        };
        list.getChannelByTriplet = function(onid, tsid, sid) {
          var current = currentChannel();
          return Number(current.onid) === Number(onid) &&
                 Number(current.tsid) === Number(tsid) &&
                 Number(current.sid) === Number(sid) ? current : null;
        };
        window.oipf.ChannelConfig = window.oipf.ChannelConfig || {};
        window.oipf.ChannelConfig.channelList = list;
        return window.oipf.ChannelConfig;
      }

      function defineConstant(obj, name, value) {
        try {
          if (obj[name] === undefined) {
            Object.defineProperty(obj, name, {
              configurable: true,
              enumerable: true,
              writable: true,
              value: value
            });
          }
        } catch (ignore) {
          try { obj[name] = value; } catch (ignoreAssign) {}
        }
      }

      function ensureBroadcastObject(obj) {
        if (!obj)
          return obj;
        try {
          defineConstant(obj, "COMPONENT_TYPE_VIDEO", 0);
          defineConstant(obj, "COMPONENT_TYPE_AUDIO", 1);
          defineConstant(obj, "COMPONENT_TYPE_SUBTITLE", 2);
          if (typeof obj.playState !== "number")
            obj.playState = 0;
          obj.currentChannel = currentChannel();
          if (obj.style)
            obj.style.background = "transparent";
        } catch (ignore) {}
        return obj;
      }

      function isConnected(obj) {
        try {
          if (!obj)
            return false;
          if (obj.isConnected !== undefined)
            return !!obj.isConnected;
          return !!(obj.ownerDocument && obj.ownerDocument.documentElement &&
                    obj.ownerDocument.documentElement.contains(obj));
        } catch (ignore) {
          return false;
        }
      }

      function isVisible(obj) {
        try {
          var rect = obj.getBoundingClientRect();
          var style = window.getComputedStyle ? window.getComputedStyle(obj) : null;
          return rect && rect.width > 0 && rect.height > 0 &&
                 (!style || (style.display !== "none" && style.visibility !== "hidden" && style.opacity !== "0"));
        } catch (ignore) {
          return false;
        }
      }

      function reportVideoWindow(obj) {
        try {
          if (!isConnected(obj) || !isVisible(obj)) {
            if (obj && obj.__openhbbtvEarlyBroadcastVisible) {
              send("BROADCAST_HIDDEN");
              send("UNSET_VIDEO_WINDOW");
              obj.__openhbbtvEarlyBroadcastVisible = false;
            }
            return;
          }
          var rect = obj.getBoundingClientRect();
          var payload = [
            Math.round(rect.left),
            Math.round(rect.top),
            Math.round(rect.width),
            Math.round(rect.height),
            window.innerWidth || document.documentElement.clientWidth || 1280,
            window.innerHeight || document.documentElement.clientHeight || 720
          ].join(",");
          if (payload !== obj.__openhbbtvEarlyBroadcastRect) {
            send("SET_VIDEO_WINDOW:" + payload);
            obj.__openhbbtvEarlyBroadcastRect = payload;
          }
          obj.__openhbbtvEarlyBroadcastVisible = true;
        } catch (ignore) {}
      }

      function dispatchPlayState(obj, state) {
        try {
          ensureBroadcastObject(obj);
          obj.playState = state;
          if (typeof obj.onPlayStateChange === "function")
            obj.onPlayStateChange(state);
          if (typeof obj.dispatchEvent === "function") {
            var event = document.createEvent ? document.createEvent("Event") : new Event("PlayStateChange");
            if (event.initEvent)
              event.initEvent("PlayStateChange", false, false);
            event.state = state;
            obj.dispatchEvent(event);
          }
        } catch (ignore) {}
      }

      function installMethod(name, fn) {
        if (typeof proto[name] === "function" && !proto[name].__openhbbtvEarlyVideoBroadcastShim)
          return;
        Object.defineProperty(proto, name, {
          configurable: true,
          writable: true,
          value: fn
        });
        proto[name].__openhbbtvEarlyVideoBroadcastShim = true;
      }

      installMethod("createChannelObject", function() {
        ensureBroadcastObject(this);
        return currentChannel();
      });
      installMethod("bindToCurrentChannel", function() {
        ensureBroadcastObject(this);
        reportVideoWindow(this);
        send("BROADCAST_PLAY");
        dispatchPlayState(this, 1);
        if (hbbtvDebugEnabled() || zdfBootDebugEnabled())
          log("HBBTVEARLY", "videoBroadcast.bindToCurrentChannel handled");
        return currentChannel();
      });
      installMethod("setChannel", function(channel) {
        ensureBroadcastObject(this);
        if (channel) {
          channel = normaliseChannel(channel);
          namespace().currentChannel = channel;
          this.currentChannel = channel;
          send("SET_CHANNEL:" + JSON.stringify({
            onid: channel.onid,
            tsid: channel.tsid,
            sid: channel.sid,
            ccid: channel.ccid || ""
          }));
        }
        reportVideoWindow(this);
        return true;
      });
      installMethod("prevChannel", function() {
        ensureBroadcastObject(this);
        send("PREV_CHANNEL");
        return currentChannel();
      });
      installMethod("nextChannel", function() {
        ensureBroadcastObject(this);
        send("NEXT_CHANNEL");
        return currentChannel();
      });
      installMethod("stop", function() {
        ensureBroadcastObject(this);
        namespace().lastBroadcastStopAt = Date.now ? Date.now() : (new Date()).getTime();
        send("BROADCAST_STOP");
        send("UNSET_VIDEO_WINDOW");
        dispatchPlayState(this, 0);
        return true;
      });
      installMethod("release", function() {
        ensureBroadcastObject(this);
        namespace().lastBroadcastStopAt = Date.now ? Date.now() : (new Date()).getTime();
        send("BROADCAST_STOP");
        send("UNSET_VIDEO_WINDOW");
        dispatchPlayState(this, 0);
        return true;
      });
      installMethod("getChannelConfig", function() {
        ensureBroadcastObject(this);
        return channelConfig();
      });
      installMethod("getComponents", function(type) {
        ensureBroadcastObject(this);
        return componentsFor(type);
      });
      installMethod("getCurrentActiveComponents", function(type) {
        ensureBroadcastObject(this);
        return componentsFor(type);
      });
      installMethod("selectComponent", function() {
        ensureBroadcastObject(this);
        return true;
      });
      installMethod("unselectComponent", function() {
        ensureBroadcastObject(this);
        return true;
      });
      installMethod("setFullScreen", function(state) {
        ensureBroadcastObject(this);
        try {
          if (state) {
            this.style.position = "fixed";
            this.style.left = "0px";
            this.style.top = "0px";
            this.style.width = "100vw";
            this.style.height = "100vh";
            reportVideoWindow(this);
            send("BROADCAST_PLAY");
          } else {
            send("BROADCAST_HIDDEN");
          }
          if (typeof this.onFullScreenChange === "function")
            this.onFullScreenChange(state);
        } catch (ignore) {}
        return true;
      });
      installMethod("addStreamEventListener", function(url, eventName, listener) {
        var ns = namespace();
        ns.streamEventListeners = ns.streamEventListeners || [];
        ns.streamEventListeners.push({ url: url, eventName: eventName, listener: listener });
        return true;
      });
      installMethod("removeStreamEventListener", function(url, eventName, listener) {
        var ns = namespace();
        var listeners = ns.streamEventListeners || [];
        for (var i = listeners.length - 1; i >= 0; i--) {
          if (listeners[i].url === url && listeners[i].eventName === eventName && listeners[i].listener === listener)
            listeners.splice(i, 1);
        }
        return true;
      });

      Object.defineProperty(proto, "__openhbbtvEarlyVideoBroadcastShim", {
        configurable: true,
        value: true
      });

      function looksLikeBroadcastObject(node) {
        try {
          if (!node || String(node.tagName || "").toUpperCase() !== "OBJECT")
            return false;
          var type = String(node.type || node.getAttribute("type") || "").toLowerCase();
          return node.id === "videoBroadcast" || type.indexOf("video/broadcast") === 0;
        } catch (ignore) {
          return false;
        }
      }

      function scan(root) {
        try {
          if (looksLikeBroadcastObject(root))
            ensureBroadcastObject(root);
          if (root && typeof root.querySelectorAll === "function") {
            var nodes = root.querySelectorAll("object");
            for (var i = 0; i < nodes.length; i++) {
              if (looksLikeBroadcastObject(nodes[i]))
                ensureBroadcastObject(nodes[i]);
            }
          }
        } catch (ignore) {}
      }

      scan(document.documentElement);
      if (window.MutationObserver && document.documentElement) {
        var observer = new MutationObserver(function(mutations) {
          for (var i = 0; i < mutations.length; i++) {
            var mutation = mutations[i];
            if (mutation.type === "childList") {
              for (var j = 0; j < mutation.addedNodes.length; j++)
                scan(mutation.addedNodes[j]);
            } else if (mutation.type === "attributes") {
              scan(mutation.target);
            }
          }
        });
        observer.observe(document.documentElement, {
          subtree: true,
          childList: true,
          attributes: true,
          attributeFilter: ["id", "type", "style", "class"]
        });
      }

      if (earlyBroadcastDebugEnabled())
        log("HBBTVEARLY", "video/broadcast prototype shim installed");
    } catch (error) {
      if (hbbtvDebugEnabled() || zdfBootDebugEnabled() || window.OPENHBBTV_API_AUDIT_DEBUG === true)
        log("HBBTVEARLY", "video/broadcast shim failed " + stackText(error));
    }
  }

  function installZdfConsoleFlag() {
    if (!zdfConsoleQueryEnabled() || !isZdfPage())
      return;
    try {
      if (/[?&]console=(1|true)(?:&|$)/i.test(window.location.search))
        return;
      var nextUrl = "";
      if (typeof URL === "function") {
        var url = new URL(window.location.href);
        url.searchParams.set("console", "1");
        nextUrl = url.toString();
      } else {
        nextUrl = window.location.href + (window.location.search ? "&" : "?") + "console=1";
      }
      window.history.replaceState(window.history.state, document.title, nextUrl);
      log("ZDFBOOT", "console query enabled " + nextUrl);
    } catch (error) {
      log("ZDFBOOT", "console query failed " + stackText(error));
    }
  }

  function installZdfBootTrace() {
    if (!zdfBootTraceEnabled() || !isZdfPage() || window.__openhbbtvZdfBootTraceInstalled)
      return;

    try {
      window.__openhbbtvZdfBootTraceInstalled = true;
      var traceCount = 0;

      function limitedTrace(label, text) {
        if (traceCount >= 120)
          return;
        traceCount++;
        zdfTrace(label + " #" + traceCount, text);
      }

      limitedTrace("installed", "href=" + window.location.href +
          " entriesStable=" + !!(Object.entries && Object.entries.__openhbbtvZdfStableObjectEntries) +
          " fromEntriesStable=" + !!(Object.fromEntries && Object.fromEntries.__openhbbtvZdfStableObjectEntries));

      if (Object.entries && !Object.entries.__openhbbtvZdfBootTrace) {
        var originalEntries = Object.entries;
        var tracedEntries = function(obj) {
          try {
            var result = originalEntries.apply(Object, arguments);
            if (Array.isArray(result)) {
              for (var i = 0; i < result.length && i < 24; i++) {
                if (!result[i] || result[i].length < 2) {
                  limitedTrace("Object.entries invalid entry",
                      "i=" + i + " len=" + result.length +
                      " entry=" + describeValue(result[i]) +
                      " arg=" + describeValue(obj) +
                      " caller=" + stackText(new Error("entries caller")));
                }
              }
            }
            return result;
          } catch (error) {
            limitedTrace("Object.entries failed",
                "arg=" + describeValue(obj) +
                " error=" + stackText(error) +
                " caller=" + stackText(new Error("entries caller")));
            throw error;
          }
        };
        tracedEntries.__openhbbtvZdfBootTrace = true;
        if (originalEntries.__openhbbtvZdfStableObjectEntries)
          tracedEntries.__openhbbtvZdfStableObjectEntries = true;
        Object.entries = tracedEntries;
      }

      if (Object.fromEntries && !Object.fromEntries.__openhbbtvZdfBootTrace) {
        var originalFromEntries = Object.fromEntries;
        var tracedFromEntries = function(iterable) {
          try {
            if (iterable === null || iterable === undefined) {
              limitedTrace("Object.fromEntries empty input",
                  "iterable=" + describeValue(iterable) +
                  " caller=" + stackText(new Error("fromEntries caller")));
            } else {
              var object = Object(iterable);
              var length = Number(object.length);
              if (length === length && length >= 0) {
                for (var i = 0; i < length && i < 24; i++) {
                  if (!(i in object)) {
                    limitedTrace("Object.fromEntries input hole",
                        "i=" + i + " len=" + length +
                        " caller=" + stackText(new Error("fromEntries caller")));
                  } else {
                    var entry = object[i];
                    var entryLooksShort = false;
                    if (entry !== null && entry !== undefined &&
                        (typeof entry === "object" || typeof entry === "function")) {
                      var entryObject = Object(entry);
                      var entryLength = Number(entryObject.length);
                      entryLooksShort = entryLength === entryLength ? entryLength < 2 :
                          (!(0 in entryObject) || !(1 in entryObject));
                    }
                    if (entry === null || entry === undefined ||
                        (typeof entry !== "object" && typeof entry !== "function") ||
                        entryLooksShort) {
                      limitedTrace("Object.fromEntries suspect entry",
                          "i=" + i + " len=" + length +
                          " entry=" + describeValue(entry) +
                          " caller=" + stackText(new Error("fromEntries caller")));
                    }
                  }
                }
              }
            }
            return originalFromEntries.apply(Object, arguments);
          } catch (error) {
            limitedTrace("Object.fromEntries failed",
                "arg=" + describeValue(iterable) +
                " error=" + stackText(error) +
                " caller=" + stackText(new Error("fromEntries caller")));
            throw error;
          }
        };
        tracedFromEntries.__openhbbtvZdfBootTrace = true;
        if (originalFromEntries.__openhbbtvZdfStableObjectEntries)
          tracedFromEntries.__openhbbtvZdfStableObjectEntries = true;
        Object.fromEntries = tracedFromEntries;
      }

      if (Array.prototype.map && !Array.prototype.map.__openhbbtvZdfBootTrace) {
        var originalMap = Array.prototype.map;
        var tracedMap = function(callback, thisArg) {
          if (typeof callback !== "function")
            return originalMap.apply(this, arguments);
          var source = this;
          var wrappedCallback = function(value, index, object) {
            try {
              return callback.call(this, value, index, object);
            } catch (error) {
              limitedTrace("Array.map callback failed",
                  "len=" + (source && source.length !== undefined ? source.length : "?") +
                  " index=" + index +
                  " value=" + describeValue(value) +
                  " object=" + describeValue(object) +
                  " error=" + stackText(error) +
                  " caller=" + stackText(new Error("map caller")));
              throw error;
            }
          };
          try {
            return originalMap.call(source, wrappedCallback, thisArg);
          } catch (error) {
            if (zdfTraceErrorWanted(error)) {
              limitedTrace("Array.map failed",
                  "len=" + (source && source.length !== undefined ? source.length : "?") +
                  " first=" + describeValue(source && source[0]) +
                  " error=" + stackText(error));
            }
            throw error;
          }
        };
        tracedMap.__openhbbtvZdfBootTrace = true;
        Array.prototype.map = tracedMap;
      }

      if (window.Promise && Promise.prototype && Promise.prototype.then &&
          !Promise.prototype.then.__openhbbtvZdfBootTrace) {
        var originalThen = Promise.prototype.then;
        var tracedThen = function(onFulfilled, onRejected) {
          var wrappedFulfilled = onFulfilled;
          if (typeof onFulfilled === "function") {
            wrappedFulfilled = function(value) {
              try {
                return onFulfilled.apply(this, arguments);
              } catch (error) {
                if (zdfTraceErrorWanted(error)) {
                  limitedTrace("Promise onFulfilled threw",
                      "value=" + describeValue(value) +
                      " error=" + stackText(error) +
                      " caller=" + stackText(new Error("promise caller")));
                }
                throw error;
              }
            };
          }

          var wrappedRejected = onRejected;
          if (typeof onRejected === "function") {
            wrappedRejected = function(reason) {
              if (zdfTraceErrorWanted(reason)) {
                limitedTrace("Promise onRejected received",
                    "reason=" + stackText(reason) +
                    " caller=" + stackText(new Error("promise caller")));
              }
              try {
                return onRejected.apply(this, arguments);
              } catch (error) {
                if (zdfTraceErrorWanted(error)) {
                  limitedTrace("Promise onRejected threw",
                      "reason=" + stackText(reason) +
                      " error=" + stackText(error) +
                      " caller=" + stackText(new Error("promise caller")));
                }
                throw error;
              }
            };
          }
          return originalThen.call(this, wrappedFulfilled, wrappedRejected);
        };
        tracedThen.__openhbbtvZdfBootTrace = true;
        Promise.prototype.then = tracedThen;
      }

      if (window.addEventListener && !window.__openhbbtvZdfBootTraceEventsInstalled) {
        window.__openhbbtvZdfBootTraceEventsInstalled = true;
        window.addEventListener("error", function(event) {
          var error = event && (event.error || event.message);
          if (zdfTraceErrorWanted(error)) {
            limitedTrace("window error",
                "message=" + (event && event.message) +
                " error=" + stackText(error));
          }
        }, true);
        window.addEventListener("unhandledrejection", function(event) {
          var reason = event && event.reason;
          if (zdfTraceErrorWanted(reason)) {
            limitedTrace("window unhandledrejection",
                "reason=" + stackText(reason));
          }
        }, true);
      }
    } catch (error) {
      zdfTrace("install failed", stackText(error));
    }
  }

  function installZdfSafeInitFetch() {
    if (!zdfSafeInitFetchEnabled() || !isZdfPage() || window.__openhbbtvZdfSafeInitFetchLoopInstalled)
      return;

    function copyResponseProperty(target, source, key) {
      try {
        target[key] = source[key];
      } catch (ignore) {}
    }

    function makeSafeInitResponse(response, body, url) {
      var safe = {};
      copyResponseProperty(safe, response, "headers");
      copyResponseProperty(safe, response, "ok");
      copyResponseProperty(safe, response, "redirected");
      copyResponseProperty(safe, response, "status");
      copyResponseProperty(safe, response, "statusText");
      copyResponseProperty(safe, response, "type");
      copyResponseProperty(safe, response, "url");
      safe.text = function() {
        zdfTrace("safe init text delivered", "url=" + url + " len=" + String(body || "").length);
        return Promise.resolve(body);
      };
      safe.json = function() {
        return Promise.resolve(JSON.parse(String(body || "")));
      };
      safe.clone = function() {
        return makeSafeInitResponse(response, body, url);
      };
      return safe;
    }

    function wrapFetch() {
      try {
        if (typeof window.fetch !== "function" || window.fetch.__openhbbtvZdfSafeInitFetch)
          return;
        var originalFetch = window.fetch;
        var wrappedFetch = function(input, init) {
          var url = "";
          try {
            url = typeof input === "string" ? input : (input && input.url) || "";
          } catch (ignore) {}
          if (!isZdfInitUrl(url))
            return originalFetch.apply(this, arguments);

          zdfTrace("safe init fetch begin", "url=" + url);
          var result;
          try {
            result = originalFetch.apply(this, arguments);
          } catch (error) {
            zdfTrace("safe init fetch threw", "url=" + url + " error=" + stackText(error));
            throw error;
          }

          return Promise.resolve(result).then(function(response) {
            zdfTrace("safe init fetch response",
                "url=" + url +
                " status=" + (response && response.status) +
                " ok=" + (response && response.ok) +
                " text=" + (response && typeof response.text));
            if (!response || typeof response.text !== "function")
              return response;
            var textResult;
            try {
              textResult = response.text();
            } catch (error) {
              zdfTrace("safe init text threw", "url=" + url + " error=" + stackText(error));
              throw error;
            }
            return Promise.resolve(textResult).then(function(body) {
              var text = String(body || "");
              zdfTrace("safe init text cached",
                  "url=" + url +
                  " len=" + text.length +
                  summarizeJson(text));
              logZdfInitDetails(url, text);
              return makeSafeInitResponse(response, text, url);
            }, function(error) {
              zdfTrace("safe init text rejected", "url=" + url + " error=" + stackText(error));
              throw error;
            });
          }, function(error) {
            zdfTrace("safe init fetch rejected", "url=" + url + " error=" + stackText(error));
            throw error;
          });
        };
        wrappedFetch.__openhbbtvZdfSafeInitFetch = true;
        wrappedFetch.__openhbbtvOriginalFetch = originalFetch;
        window.fetch = wrappedFetch;
        zdfTrace("safe init fetch wrapped", "fetch=" + describeValue(originalFetch));
      } catch (error) {
        zdfTrace("safe init fetch wrap failed", stackText(error));
      }
    }

    window.__openhbbtvZdfSafeInitFetchLoopInstalled = true;
    wrapFetch();
    setTimeout(wrapFetch, 0);
    setTimeout(wrapFetch, 50);
    setTimeout(wrapFetch, 250);
    setTimeout(wrapFetch, 1000);
    setTimeout(wrapFetch, 2000);
  }

  function stringifyConsoleArg(arg) {
    try {
      if (arg instanceof Error)
        return arg.name + ": " + arg.message + " " + stackText(arg);
      if (arg === undefined)
        return "undefined";
      if (arg === null)
        return "null";
      if (typeof arg === "string")
        return arg;
      var json = JSON.stringify(arg);
      return json === undefined ? String(arg) : json;
    } catch (ignore) {
      return String(arg);
    }
  }

  function installZdfConsoleBridge() {
    if (!zdfConsoleDebugEnabled() || !isZdfPage() || !window.console || window.console.__openhbbtvZdfBridge)
      return;
    try {
      var levels = ["log", "info", "warn", "error", "debug"];
      var count = 0;
      for (var i = 0; i < levels.length; i++) {
        (function(level) {
          var original = typeof window.console[level] === "function" ? window.console[level] : function() {};
          window.console[level] = function() {
            try {
              if (count < 120) {
                var parts = [];
                for (var j = 0; j < arguments.length; j++)
                  parts.push(stringifyConsoleArg(arguments[j]));
                count++;
                log("ZDFCONSOLE", level + " " + parts.join(" "));
              }
            } catch (ignore) {}
            return original.apply(window.console, arguments);
          };
        })(levels[i]);
      }
      window.console.__openhbbtvZdfBridge = true;
      log("ZDFBOOT", "console bridge installed");
    } catch (error) {
      log("ZDFBOOT", "console bridge failed " + stackText(error));
    }
  }

  function installZdfBootstrapProbe() {
    if (!zdfBootDebugEnabled() || !zdfDeepProbeEnabled() || !isZdfPage())
      return;

    try {
      if (Object.entries && !Object.entries.__openhbbtvZdfProbe) {
        var originalEntries = Object.entries;
        var wrappedEntries = function(obj) {
          try {
            var result = originalEntries.apply(Object, arguments);
            if (Array.isArray(result)) {
              for (var i = 0; i < result.length && i < 16; i++) {
                if (!result[i] || result[i].length < 2)
                  log("ZDFBOOT", "Object.entries invalid i=" + i + " len=" + result.length +
                      " entry=" + describeValue(result[i]) + " caller=" + stackText(new Error("caller")));
              }
            }
            return result;
          } catch (error) {
            log("ZDFBOOT", "Object.entries failed arg=" + describeValue(obj) +
                " error=" + stackText(error) + " caller=" + stackText(new Error("caller")));
            throw error;
          }
        };
        wrappedEntries.__openhbbtvZdfProbe = true;
        Object.entries = wrappedEntries;
      }

      if (Object.fromEntries && !Object.fromEntries.__openhbbtvZdfProbe) {
        var originalFromEntries = Object.fromEntries;
        var wrappedFromEntries = function(iterable) {
          try {
            if (!iterable)
              log("ZDFBOOT", "Object.fromEntries empty iterable=" + describeValue(iterable) +
                  " caller=" + stackText(new Error("caller")));
            return originalFromEntries.apply(Object, arguments);
          } catch (error) {
            log("ZDFBOOT", "Object.fromEntries failed arg=" + describeValue(iterable) +
                " error=" + stackText(error) + " caller=" + stackText(new Error("caller")));
            throw error;
          }
        };
        wrappedFromEntries.__openhbbtvZdfProbe = true;
        Object.fromEntries = wrappedFromEntries;
      }

      if (Array.prototype.map && !Array.prototype.map.__openhbbtvZdfProbe) {
        var originalMap = Array.prototype.map;
        var wrappedMap = function() {
          try {
            return originalMap.apply(this, arguments);
          } catch (error) {
            log("ZDFBOOT", "Array.map failed len=" + (this && this.length !== undefined ? this.length : "?") +
                " first=" + describeValue(this && this[0]) + " error=" + stackText(error));
            throw error;
          }
        };
        wrappedMap.__openhbbtvZdfProbe = true;
        Array.prototype.map = wrappedMap;
      }

      if (window.Promise && Promise.prototype && Promise.prototype.then &&
          !Promise.prototype.then.__openhbbtvZdfProbe) {
        var originalThen = Promise.prototype.then;
        var rejectionCount = 0;
        var wrappedThen = function(onFulfilled, onRejected) {
          var wrappedRejected = onRejected;
          if (typeof onRejected === "function") {
            wrappedRejected = function(reason) {
              try {
                var text = stackText(reason);
                if (rejectionCount < 80 &&
                    (text.indexOf("Cannot read property '0' of undefined") >= 0 ||
                     text.indexOf("Cannot read properties of undefined") >= 0 ||
                     text.indexOf("appConfigLoader getInit") >= 0 ||
                     text.indexOf("bindToCurrentChannel") >= 0)) {
                  rejectionCount++;
                  logPieces("ZDFBOOT", "Promise rejection " + rejectionCount,
                      text + " caller=" + stackText(new Error("promise caller")));
                }
              } catch (ignore) {}
              return onRejected.apply(this, arguments);
            };
          }
          return originalThen.call(this, onFulfilled, wrappedRejected);
        };
        wrappedThen.__openhbbtvZdfProbe = true;
        Promise.prototype.then = wrappedThen;
      }

      if (window.Promise && Promise.allSettled && !Promise.allSettled.__openhbbtvZdfProbe) {
        var originalAllSettled = Promise.allSettled;
        var wrappedAllSettled = function(iterable) {
          try {
            if (!iterable)
              log("ZDFBOOT", "Promise.allSettled empty iterable=" + describeValue(iterable) +
                  " caller=" + stackText(new Error("caller")));
            return originalAllSettled.apply(Promise, arguments);
          } catch (error) {
            log("ZDFBOOT", "Promise.allSettled failed arg=" + describeValue(iterable) +
                " error=" + stackText(error));
            throw error;
          }
        };
        wrappedAllSettled.__openhbbtvZdfProbe = true;
        Promise.allSettled = wrappedAllSettled;
      }

      log("ZDFBOOT", "bootstrap probe installed");
    } catch (error) {
      log("ZDFBOOT", "bootstrap probe failed " + stackText(error));
    }
  }

  try {
    installZdfStableObjectEntries();
    installZdfBootTrace();
    installZdfSafeInitFetch();
    installEarlyVideoBroadcastShim();
    installZdfConsoleFlag();
    installZdfConsoleBridge();
    installZdfBootstrapProbe();
  } catch (error) {
    log("ZDFBOOT", "diagnostics install failed " + stackText(error));
  }

  function installChunkProbe(name) {
    try {
      var target = window[name];
      if (target && target.__openhbbtvChunkProbeArray)
        return;
      if (!target)
        target = [];
      var assignedPush = Array.prototype.push;
      var wrappedPush = function(data) {
        if (!data || !data[0] || !data[1]) {
          log("HBBTVJS", "chunk push invalid name=" + name + " type=" + Object.prototype.toString.call(data) +
              " value=" + String(data));
        }
        return assignedPush.apply(target, arguments);
      };
      Object.defineProperty(target, "push", {
        configurable: true,
        get: function() {
          return wrappedPush;
        },
        set: function(fn) {
          assignedPush = typeof fn === "function" ? fn : Array.prototype.push;
        }
      });
      target.__openhbbtvChunkProbeArray = true;
      window[name] = target;
    } catch (error) {
      log("HBBTVJS", "chunk probe install failed name=" + name + " error=" + error);
    }
  }

  if (hbbtvDebugEnabled() && zdfDeepProbeEnabled()) {
    installChunkProbe("webpackChunk_tv_media_library_zdf_mediathek");
  }

  try {
    if (!jsHttpWrapDebugEnabled())
      throw new Error("disabled");
    var originalOpen = XMLHttpRequest.prototype.open;
    var originalSend = XMLHttpRequest.prototype.send;
    XMLHttpRequest.prototype.open = function(method, url) {
      this.__openhbbtvAuthMethod = method || "GET";
      this.__openhbbtvAuthUrl = String(url || "");
      return originalOpen.apply(this, arguments);
    };
    XMLHttpRequest.prototype.send = function() {
      var labels = labelsFor(this.__openhbbtvAuthUrl);
      if (labels.length) {
        var xhr = this;
        var requestBody = bodyText(arguments.length ? arguments[0] : "");
        xhr.addEventListener("loadend", function() {
          var readBody = shouldReadResponseBody(labels, xhr.__openhbbtvAuthUrl);
          var body = "";
          if (readBody) {
            try {
              if (typeof xhr.responseText === "string")
                body = xhr.responseText;
            } catch (ignore) {}
          }
          labels.forEach(function(label) {
            log(label, "XHR " + xhr.__openhbbtvAuthMethod + " " + xhr.__openhbbtvAuthUrl +
                " status=" + xhr.status + " responseURL=" + (xhr.responseURL || "") +
                (requestBody ? " request=" + requestBody : "") +
                (readBody ? responseDetail(label, xhr.__openhbbtvAuthUrl, body) : ""));
          });
          if (readBody)
            logZdfInitDetails(xhr.__openhbbtvAuthUrl, body);
        });
      }
      return originalSend.apply(this, arguments);
    };
  } catch (error) {
    if (jsHttpWrapDebugEnabled()) {
      log("AUTHHTTP", "XHR debug install failed " + error);
      log("HBBTVHTTP", "XHR debug install failed " + error);
    }
  }

  try {
    if (jsHttpWrapDebugEnabled() && typeof window.fetch === "function" && !window.fetch.__openhbbtvAuthWrapped) {
      var originalFetch = window.fetch;
      var wrappedFetch = function(input, init) {
        var url = "";
        try {
          url = typeof input === "string" ? input : (input && input.url) || "";
        } catch (ignore) {}
        var method = (init && init.method) || (input && input.method) || "GET";
        var requestBody = bodyText(init && init.body);
        return originalFetch.apply(this, arguments).then(function(response) {
          var labels = labelsFor(url);
          installZdfResponseTextProbe(url, response);
          if (labels.length) {
            if (!shouldReadResponseBody(labels, url)) {
              labels.forEach(function(label) {
                log(label, "fetch " + method + " " + url +
                    " status=" + response.status +
                    " responseURL=" + (response.url || "") +
                    (requestBody ? " request=" + requestBody : ""));
              });
              return response;
            }
            try {
              response.clone().text().then(function(body) {
                labels.forEach(function(label) {
                  log(label, "fetch " + method + " " + url +
                      " status=" + response.status +
                      " responseURL=" + (response.url || "") +
                      (requestBody ? " request=" + requestBody : "") +
                      responseDetail(label, url, body));
                });
                logZdfInitDetails(url, body);
              }).catch(function(error) {
                labels.forEach(function(label) {
                  log(label, "fetch body failed " + method + " " + url +
                      " status=" + response.status + " error=" + error);
                });
              });
            } catch (error) {
              labels.forEach(function(label) {
                log(label, "fetch debug failed " + method + " " + url +
                    " status=" + response.status + " error=" + error);
              });
            }
          }
          return response;
        });
      };
      wrappedFetch.__openhbbtvAuthWrapped = true;
      window.fetch = wrappedFetch;
    }
  } catch (error) {
    log("AUTHHTTP", "fetch debug install failed " + error);
    log("HBBTVHTTP", "fetch debug install failed " + error);
  }
})();
