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

  function installChunkProbe(name) {
    try {
      var target = window[name];
      if (!target || !target.__openhbbtvChunkProbeArray)
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

  if (hbbtvDebugEnabled()) {
    installChunkProbe("webpackChunk_tv_media_library_zdf_mediathek");
  }

  try {
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
          var body = "";
          try {
            if (typeof xhr.responseText === "string")
              body = xhr.responseText;
          } catch (ignore) {}
          labels.forEach(function(label) {
            log(label, "XHR " + xhr.__openhbbtvAuthMethod + " " + xhr.__openhbbtvAuthUrl +
                " status=" + xhr.status + " responseURL=" + (xhr.responseURL || "") +
                (requestBody ? " request=" + requestBody : "") +
                responseDetail(label, xhr.__openhbbtvAuthUrl, body));
          });
        });
      }
      return originalSend.apply(this, arguments);
    };
  } catch (error) {
    log("AUTHHTTP", "XHR debug install failed " + error);
    log("HBBTVHTTP", "XHR debug install failed " + error);
  }

  try {
    if (typeof window.fetch === "function" && !window.fetch.__openhbbtvAuthWrapped) {
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
          if (labels.length) {
            try {
              response.clone().text().then(function(body) {
                labels.forEach(function(label) {
                  log(label, "fetch " + method + " " + url +
                      " status=" + response.status +
                      " responseURL=" + (response.url || "") +
                      (requestBody ? " request=" + requestBody : "") +
                      responseDetail(label, url, body));
                });
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
