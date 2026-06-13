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
}

(function() {
  if (window.__openhbbtvAuthHttpDebugInstalled)
    return;
  window.__openhbbtvAuthHttpDebugInstalled = true;

  function enabled() {
    return window.OPENHBBTV_AUTH_HTTP_DEBUG !== false;
  }

  function shouldLog(url) {
    url = String(url || "");
    return url.indexOf("accounts.ard.de/device/") >= 0 ||
           url.indexOf("accounts.ard.de/id") >= 0 ||
           url.indexOf("tv.ardmediathek.de/dyn/get?id=login") >= 0;
  }

  function mask(text) {
    text = String(text || "");
    text = text.replace(/(accounts\.ard\.de\/device\/token\/)([A-Z0-9]{5})\+[^"'\s<>)&]+/g, "$1$2+***");
    text = text.replace(/("token_uri"\s*:\s*"https:\/\/accounts\.ard\.de\/device\/token\/)([A-Z0-9]{5})\+[^"]+(")/g, "$1$2+***$3");
    text = text.replace(/("(?:access_token|refresh_token|id_token|device_secret|token)"\s*:\s*")[^"]+(")/g, "$1***$2");
    return text.length > 600 ? text.slice(0, 600) + "..." : text;
  }

  function log(message) {
    if (!enabled())
      return;
    try {
      if (window.signalopenhbbtvbrowser)
        window.signalopenhbbtvbrowser("LOG:AUTHHTTP " + mask(message));
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
      if (shouldLog(this.__openhbbtvAuthUrl)) {
        var xhr = this;
        xhr.addEventListener("loadend", function() {
          var body = "";
          try {
            if (typeof xhr.responseText === "string")
              body = xhr.responseText;
          } catch (ignore) {}
          log("XHR " + xhr.__openhbbtvAuthMethod + " " + xhr.__openhbbtvAuthUrl +
              " status=" + xhr.status + " responseURL=" + (xhr.responseURL || "") +
              " body=" + body);
        });
      }
      return originalSend.apply(this, arguments);
    };
  } catch (error) {
    log("XHR debug install failed " + error);
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
        return originalFetch.apply(this, arguments).then(function(response) {
          if (shouldLog(url)) {
            try {
              response.clone().text().then(function(body) {
                log("fetch " + method + " " + url +
                    " status=" + response.status +
                    " responseURL=" + (response.url || "") +
                    " body=" + body);
              }).catch(function(error) {
                log("fetch body failed " + method + " " + url +
                    " status=" + response.status + " error=" + error);
              });
            } catch (error) {
              log("fetch debug failed " + method + " " + url +
                  " status=" + response.status + " error=" + error);
            }
          }
          return response;
        });
      };
      wrappedFetch.__openhbbtvAuthWrapped = true;
      window.fetch = wrappedFetch;
    }
  } catch (error) {
    log("fetch debug install failed " + error);
  }
})();
