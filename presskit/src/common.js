/* Shared helpers for the Dova Stash art pages.
   - q(name, def): read a query-string parameter
   - body is hidden until both font families have loaded; if any fails,
     the page is crossed out (class "fontfail") so a fallback can never ship. */
window.q = function (name, def) {
  var v = new URLSearchParams(location.search).get(name);
  return v === null ? def : v;
};
(function () {
  var b = document.documentElement;
  b.style.visibility = "hidden";
  // fixed canvas size (render.py passes the target pixel size)
  if (q("w", "")) {
    var w = +q("w"), h = +q("h");
    if (q("frame", "")) {
      // Description card (?frame=<margin px>): the art keeps its full size as a rounded black
      // card with a hairline edge, and the canvas grows by the margin on every side for a soft
      // shadow, so it sits on the Nexus page instead of as a hard rectangle. Same geometry as
      // the iharvest-ng description cards (1200x112 header -> 1244x156 canvas).
      var m = +q("frame");
      b.classList.add("framed");
      b.style.width = w + 2 * m + "px";
      b.style.height = h + 2 * m + "px";
      b.style.setProperty("--fm", m + "px");
    }
    b.style.setProperty("--W", w + "px");
    b.style.setProperty("--H", h + "px");
  }
  if (q("bg", "") === "transparent") {
    document.addEventListener("DOMContentLoaded", function () {
      document.body.classList.add("transparent");
    });
  }
  var loads = [
    document.fonts.load('600 40px "DS Cinzel"', "DOVA STASH"),
    document.fonts.load('400 40px "DS Jost"', "Every home")
  ];
  Promise.all(loads).then(function (res) {
    var ok = res.every(function (r) { return r && r.length > 0; });
    if (!ok) document.body.classList.add("fontfail");
    b.style.visibility = "visible";
  }, function () {
    document.body.classList.add("fontfail");
    b.style.visibility = "visible";
  });
})();

/* Inline an SVG from icons/ so stroke weight can be styled from the page.
   Needs Edge/Chrome started with --allow-file-access-from-files (render.py
   does this). The file's own <style> is dropped; style.css styles .ico/.logo. */
window.svgFile = function (name) {
  var x = new XMLHttpRequest();
  x.open("GET", "icons/" + name + ".svg", false);
  x.send();
  return x.responseText.replace(/<style>[\s\S]*?<\/style>/, "");
};
/* icon(name, px[, stroke px]) -> markup for a line icon at px size */
window.icon = function (name, px, sw) {
  var s = svgFile(name).replace(/width="256" height="256"/,
    'width="' + px + '" height="' + px + '" class="ico"' +
    (sw ? ' style="--sw:' + sw + 'px"' : ""));
  return s;
};
/* mark(px, markStroke, frameStroke) -> the logo emblem; strokes in 256-units */
window.mark = function (px, mw, fw) {
  return svgFile("logo").replace(/width="256" height="256"/,
    'width="' + px + '" height="' + px + '" class="logo" style="--mw:' + mw + ";--fw:" + fw + '"');
};
