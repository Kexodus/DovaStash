/* Dova Stash Editor user guide: page chrome, contents, chapter heads and screenshot overlays.
   Everything is drawn in the browser; build.py prints the result to PDF with headless Edge. */

(function () {
  "use strict";
  var root = document.documentElement;
  root.style.visibility = "hidden";

  /* The Dova Stash emblem (presskit/src/icons/logo.svg), strokes in 256-units. */
  function mark(px, mw, fw, frame) {
    // Without the frame (dividers) the box is cropped to the mark itself, as divider.html does.
    var box = frame === false ? '48 58 160 144" width="' + px + '" height="' + Math.round(px * 144 / 160)
                              : '0 0 256 256" width="' + px + '" height="' + px;
    return '<svg class="logo" viewBox="' + box + '" style="display:block;overflow:visible">' +
      (frame === false ? "" : '<path d="M44 16 H212 L240 44 V212 L212 240 H44 L16 212 V44 Z" fill="none" stroke="#fff" stroke-width="' + fw + '" stroke-linejoin="miter"/>') +
      '<g transform="translate(0 -10)" fill="none" stroke="#fff" stroke-width="' + mw + '" stroke-linecap="square" stroke-linejoin="miter" stroke-miterlimit="8">' +
      '<path d="M152 76 L56 140 H200 L104 76"/><path d="M56 140 V204 H200 V140"/><path d="M104 140 V204 M152 140 V204"/></g></svg>';
  }
  window.dsMark = mark;

  /* Line icons from presskit/src/icons (needs --allow-file-access-from-files, as render.py uses). */
  function icon(name, px, sw) {
    var x = new XMLHttpRequest();
    x.open("GET", "../../src/icons/" + name + ".svg", false);
    x.send();
    var s = x.responseText.replace(/<style>[\s\S]*?<\/style>/, "");
    s = s.replace(/width="256" height="256"/, 'width="' + px + '" height="' + px + '" class="ico"');
    return s.replace("<svg ", '<svg style="--sw:' + (sw || 1.5) + 'px" ');
  }

  function esc(t) { return String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;"); }
  function two(n) { return (n < 10 ? "0" : "") + n; }

  /* ---------------------------------------------------------------- figures
     FIGS[key] = { src, size: [w, h] of the source image, crop: [x, y, w, h] (source px),
                   width (display px, default the column), callouts: [{ box: [x0,y0,x1,y1], at?: [x,y], c?: "tl"|"tr"|"bl"|"br" }],
                   caption } */
  function buildFigure(el, figNo) {
    var f = window.FIGS[el.getAttribute("data-fig")];
    if (!f) { el.innerHTML = "<p>missing figure " + esc(el.getAttribute("data-fig")) + "</p>"; return; }
    var size = f.size || [1600, 900];
    var crop = f.crop || [0, 0, size[0], size[1]];
    var W = f.width || el.clientWidth || el.parentElement.clientWidth || 688;
    var s = W / crop[2];
    var H = Math.round(crop[3] * s);
    function X(v) { return (v - crop[0]) * s; }
    function Y(v) { return (v - crop[1]) * s; }

    var svg = '<svg class="ov" width="' + W + '" height="' + H + '">';
    var badges = "";
    (f.callouts || []).forEach(function (o, i) {
      var pad = 4;
      var x0 = X(o.box[0]) - pad, y0 = Y(o.box[1]) - pad, x1 = X(o.box[2]) + pad, y1 = Y(o.box[3]) + pad;
      x0 = Math.max(x0, 2); y0 = Math.max(y0, 2); x1 = Math.min(x1, W - 2); y1 = Math.min(y1, H - 2);
      var arm = Math.max(4, Math.min(12, (x1 - x0) / 3, (y1 - y0) / 3));
      var d = "M" + x0 + "," + (y0 + arm) + "V" + y0 + "H" + (x0 + arm) +
              "M" + (x1 - arm) + "," + y0 + "H" + x1 + "V" + (y0 + arm) +
              "M" + x1 + "," + (y1 - arm) + "V" + y1 + "H" + (x1 - arm) +
              "M" + (x0 + arm) + "," + y1 + "H" + x0 + "V" + (y1 - arm);
      var r = 9, bx, by, leader = "";
      if (o.at) {
        bx = X(o.at[0]); by = Y(o.at[1]);
        var sx = Math.min(Math.max(bx, x0), x1), sy = Math.min(Math.max(by, y0), y1);
        leader = "M" + sx + "," + sy + "L" + bx + "," + by;
      } else if (o.c === "r" || o.c === "l") {
        // beside the box, vertically centred (rows of a toolbar)
        bx = o.c === "r" ? x1 + r + 3 : x0 - r - 3;
        by = (y0 + y1) / 2;
      } else {
        var c = o.c || "tl";
        bx = c.indexOf("l") >= 0 ? x0 : x1;
        by = c.indexOf("t") >= 0 ? y0 : y1;
      }
      bx = Math.min(Math.max(bx, r + 2), W - r - 2);
      by = Math.min(Math.max(by, r + 2), H - r - 2);
      svg += '<path class="halo" d="' + d + leader + '"/><path class="br" d="' + d + '"/>';
      if (leader) svg += '<path class="ld" d="' + leader + '"/>';
      badges += '<g class="badge"><circle cx="' + bx + '" cy="' + by + '" r="' + r + '"/>' +
                '<text x="' + bx + '" y="' + (by + 0.5) + '">' + (o.n || i + 1) + "</text></g>";
    });
    svg += badges + "</svg>";

    var img = '<img src="../img/' + f.src + '" style="left:' + (-crop[0] * s) + "px;top:" + (-crop[1] * s) +
              "px;width:" + (size[0] * s) + "px;height:" + (size[1] * s) + 'px" alt="">';
    var html = '<div class="shot" style="width:' + W + "px;height:" + H + 'px">' + img + svg + "</div>";
    if (f.caption !== null) {
      html += '<figcaption><span class="fig">Figure ' + figNo + "</span>" + (f.caption || "") + "</figcaption>";
    }
    el.innerHTML = html;
  }

  function build() {
    var pages = Array.prototype.slice.call(document.querySelectorAll(".page"));

    // chapter heads
    document.querySelectorAll("header.chapter-head").forEach(function (h) {
      h.innerHTML = '<div class="bullet">' + mark(38, 11, 6) + "</div><div>" +
        '<span class="no">' + esc(h.getAttribute("data-no")) + "</span><h1>" + esc(h.getAttribute("data-title")) + "</h1></div>";
    });

    // running head and foot, page numbers
    var chapter = "";
    pages.forEach(function (p, i) {
      var head = p.querySelector("header.chapter-head");
      if (head) chapter = head.getAttribute("data-title");
      if (p.hasAttribute("data-chapter")) chapter = p.getAttribute("data-chapter");
      p.setAttribute("data-page", i + 1);
      if (p.classList.contains("cover")) return;
      p.insertAdjacentHTML("afterbegin",
        '<div class="run-head"><div class="lock">' + mark(16, 12, 7) + '<span class="word">DOVA STASH EDITOR</span></div>' +
        '<span class="chap">' + esc(chapter) + "</span></div>" +
        '<div class="run-foot"><span>User guide</span><span class="num">' + two(i + 1) + "</span></div>");
    });

    // contents
    var toc = document.getElementById("toc");
    if (toc) {
      var rows = "";
      document.querySelectorAll("header.chapter-head").forEach(function (h) {
        var page = h.closest(".page").getAttribute("data-page");
        rows += '<li><span class="n">' + esc(h.getAttribute("data-no")) + '</span><span class="t">' +
                esc(h.getAttribute("data-title")) + '</span><span class="d"></span><span class="p">' + two(+page) + "</span>" +
                (h.getAttribute("data-sub") ? '<span class="s">' + esc(h.getAttribute("data-sub")) + "</span>" : "") + "</li>";
      });
      toc.innerHTML = rows;
    }

    // icons
    document.querySelectorAll("[data-icon]").forEach(function (e) {
      e.innerHTML = icon(e.getAttribute("data-icon"), +(e.getAttribute("data-px") || 72), +(e.getAttribute("data-sw") || 1.5));
    });
    // marks
    document.querySelectorAll("[data-mark]").forEach(function (e) {
      var a = e.getAttribute("data-mark").split(",").map(Number);
      e.innerHTML = mark(a[0], a[1], a[2], a[3] !== 0);
    });

    // figures, numbered in order
    var n = 0;
    document.querySelectorAll("figure[data-fig]").forEach(function (el) { buildFigure(el, ++n); });
  }

  var loads = [
    document.fonts.load('600 40px "DS Cinzel"', "DOVA STASH"),
    document.fonts.load('400 40px "DS Jost"', "Every home")
  ];
  document.addEventListener("DOMContentLoaded", function () {
    Promise.all(loads).then(function (res) {
      var ok = res.every(function (r) { return r && r.length > 0; });
      build();
      if (!ok) document.body.insertAdjacentHTML("afterbegin", '<div style="position:fixed;inset:0;z-index:9;background:#888;font:700 60px monospace;color:#000">FONT FALLBACK</div>');
      root.style.visibility = "visible";
    });
  });
})();
