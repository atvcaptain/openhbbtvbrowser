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
    return text.length > 900 ? text.slice(0, 900) + "..." : text;
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
      var data = value && value.data && typeof value.data === "object" ? Object.keys(value.data).slice(0, 30).join(",") : "";
      var errors = value && value.errors && value.errors.length !== undefined ? String(value.errors.length) : "";
      return " jsonRoot=" + root + (data ? " dataKeys=" + data : "") + (errors ? " errors=" + errors : "");
    } catch (ignore) {
      return "";
    }
  }

  function log(label, message) {
    try {
      if (window.signalopenhbbtvbrowser)
        window.signalopenhbbtvbrowser("LOG:" + label + " " + mask(message));
    } catch (ignore) {}
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
                summarizeJson(body) + " body=" + body);
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
                      summarizeJson(body) + " body=" + body);
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
