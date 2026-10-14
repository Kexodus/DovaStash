/* Screenshots and their callouts. Boxes are in the screenshot's own pixels (1584 x 911 captures,
   see ../README.md for the commands that take them); crop = [x, y, width, height]; width = display px.
   Callouts are numbered in order (or by n); the legend under each figure uses the same numbers.
   c places the badge: tl/tr/bl/br on a corner of the box, r/l beside it (toolbar rows). */
window.FIGS = {
  hero: {
    src: "hero.jpg", size: [1600, 760], crop: [0, 0, 1600, 700],
    caption: "Breezehome as the editor draws it, from your own meshes and textures.",
    callouts: []
  },
  start: {
    src: "start.png", crop: [254, 96, 1058, 482],
    caption: "The start page, with four homes in progress.",
    callouts: [
      { box: [297, 172, 1288, 231] },
      { box: [297, 262, 862, 302] },
      { box: [297, 315, 1288, 410] },
      { box: [297, 418, 1288, 566] }
    ]
  },
  mods: {
    src: "homes-by-mod.png", crop: [0, 0, 600, 272],
    caption: "Homes by mod: every home of your load order under its mod. A name or Nexus id finds it.",
    callouts: [
      { box: [50, 8, 147, 32], c: "r" },
      { box: [8, 118, 118, 140], c: "r" },
      { box: [8, 145, 569, 167], c: "tr" },
      { box: [8, 215, 569, 234], c: "tr" },
      { box: [8, 236, 569, 255], c: "br" }
    ]
  },
  view3dBar: {
    src: "view3d-overview.jpg", crop: [0, 150, 1060, 116],
    caption: "The cell's tabs and the 3D view's toolbar.",
    callouts: [
      { n: 4, box: [8, 158, 482, 180], c: "r" },
      { n: 5, box: [8, 185, 860, 207], c: "r" },
      { n: 6, box: [8, 212, 806, 234], c: "r" },
      { n: 7, box: [8, 240, 410, 261], c: "r" }
    ]
  },
  view3d: {
    src: "view3d-overview.jpg",
    caption: "A home open in the 3D view (Whitepeak Tower, from above).",
    callouts: [
      { n: 1, box: [455, 296, 765, 815], c: "tr" },
      { n: 2, box: [1255, 270, 1574, 462], c: "tl" },
      { n: 3, box: [8, 879, 915, 900], c: "tr" }
    ]
  },
  plan: {
    src: "floor-plan.png", crop: [0, 180, 1250, 731],
    caption: "The floor plan: the same home seen from above, north up.",
    callouts: [
      { box: [8, 185, 520, 207], c: "r" },
      { box: [8, 212, 575, 234], c: "r" },
      { box: [8, 238, 325, 262], c: "r" },
      { box: [745, 484, 786, 522], c: "tr" },
      { box: [14, 868, 182, 894], c: "tr" },
      { box: [1218, 271, 1240, 291], c: "bl" }
    ]
  },
  select3d: {
    src: "view3d-select.jpg", crop: [60, 380, 1080, 420],
    caption: "Selected in 3D: the potion basket gets a yellow frame and its details open on the right.",
    callouts: []
  },
  rolePane: {
    src: "view3d-select.jpg", crop: [1253, 470, 324, 300], width: 300,
    caption: "The role editor for a container.",
    callouts: [
      { box: [1261, 497, 1342, 517], c: "r" },
      { box: [1261, 543, 1568, 565], c: "tr" },
      { box: [1261, 571, 1410, 592], c: "r" },
      { box: [1261, 616, 1568, 662], c: "tr" },
      { box: [1261, 686, 1568, 737], c: "tr" },
      { box: [1261, 741, 1421, 760], c: "r" }
    ]
  },
  ignored: {
    src: "role-ignored.jpg", crop: [1253, 462, 324, 258], width: 300,
    caption: "A respawning urn: the draft already ignores it, with the reason as its note.",
    callouts: [
      { box: [1258, 468, 1400, 486], c: "r" },
      { box: [1261, 591, 1410, 613], c: "r" },
      { box: [1261, 638, 1568, 689], c: "tr" }
    ]
  },
  containers: {
    src: "containers-table.png", crop: [0, 184, 1200, 300],
    caption: "The Containers tab lists every container with its role; click a row to select it.",
    callouts: [
      { box: [378, 228, 748, 480], c: "tr" },
      { box: [752, 228, 1122, 480], c: "tr" }
    ]
  },
  paint: {
    src: "paint.jpg", crop: [0, 234, 1250, 670],
    caption: "Paint mode with alchemy.potions picked. Colours switch to “by category” while you paint.",
    callouts: [
      { box: [8, 239, 312, 262], c: "tr" },
      { box: [318, 240, 1250, 284], c: "tr" },
      { box: [556, 530, 700, 664], c: "tr" },
      { box: [8, 879, 700, 900], c: "r" }
    ]
  },
  multi3d: {
    src: "multi-select.jpg", crop: [290, 330, 500, 560],
    caption: "Three sacks selected at once.",
    callouts: [ { box: [310, 376, 752, 846], c: "tr" } ]
  },
  multiPane: {
    src: "multi-select.jpg", crop: [1253, 270, 324, 360],
    caption: "The details pane edits them together.",
    callouts: [
      { n: 2, box: [1258, 274, 1576, 312], c: "br" },
      { n: 3, box: [1261, 341, 1380, 362], c: "r" },
      { n: 4, box: [1261, 366, 1568, 434], c: "tr" },
      { n: 5, box: [1261, 466, 1378, 488], c: "r" },
      { n: 6, box: [1261, 518, 1568, 588], c: "tr" },
      { n: 7, box: [1261, 598, 1403, 620], c: "r" }
    ]
  },
  mannequin: {
    src: "mannequin.jpg", crop: [0, 266, 1580, 640],
    caption: "A mannequin selected in Whitepeak Tower. The blue see-through boxes are weapon-rack triggers.",
    callouts: [
      { box: [483, 336, 792, 772], c: "tr" },
      { box: [200, 410, 400, 800], c: "tl" },
      { box: [1261, 489, 1460, 549], c: "tr" },
      { box: [1261, 580, 1380, 602], c: "r" },
      { box: [1261, 682, 1568, 724], c: "tr" },
      { box: [1261, 731, 1568, 794], c: "br" }
    ]
  },
  rack: {
    src: "rack.jpg", crop: [1253, 480, 324, 294], width: 290,
    caption: "A shield rack: its kind decides what fits.",
    callouts: [
      { box: [1261, 486, 1380, 502], c: "r" },
      { box: [1261, 578, 1380, 598], c: "r" },
      { box: [1261, 698, 1360, 716], c: "r" },
      { box: [1261, 745, 1470, 764], c: "r" }
    ]
  },
  draftsPane: {
    src: "drafts.png", crop: [550, 92, 1030, 672],
    caption: "Draft all homes: the draft for Aldamar, with its master and suggestions.",
    callouts: [
      { box: [556, 98, 1140, 168], c: "tr" },
      { box: [556, 176, 1568, 276], c: "tr" },
      { box: [556, 330, 1555, 676], c: "tr" },
      { box: [556, 684, 1572, 754], c: "tr" }
    ]
  },
  draftsList: {
    src: "drafts.png", crop: [10, 92, 526, 470],
    caption: "The list of homes.",
    callouts: [
      { box: [14, 96, 532, 146], c: "tr" },
      { box: [14, 151, 285, 173], c: "r" },
      { box: [14, 538, 520, 557], c: "tr" }
    ]
  },
  draftsCells: {
    src: "drafts-multicell.png", crop: [550, 96, 1030, 330],
    caption: "Lakeview Manor in the drafts view: one home over two cells, compared with its shipped template.",
    callouts: [
      { box: [556, 124, 960, 142], c: "tr" },
      { box: [556, 146, 1440, 163], c: "tr" },
      { box: [1500, 313, 1555, 401], c: "tl" }
    ]
  },
  preview: {
    src: "preview.png", crop: [0, 174, 1240, 731],
    caption: "Where items go for Aldamar's draft, with the typical haul.",
    callouts: [
      { box: [8, 185, 282, 207], c: "tl" },
      { box: [290, 185, 373, 207], c: "r" },
      { box: [8, 212, 325, 234], c: "r" },
      { box: [8, 239, 768, 256], c: "r" },
      { box: [8, 303, 1230, 322], c: "tr" },
      { box: [322, 325, 505, 765], c: "tr" },
      { box: [8, 767, 880, 900], c: "tr" }
    ]
  },
  fill3d: {
    src: "preview-3d-fill.jpg", crop: [0, 238, 1584, 673],
    caption: "Colours by fill in 3D. The master, a barrel by the door, is selected; the green box beside it gets a few items.",
    callouts: [
      { box: [413, 240, 868, 261], c: "r" },
      { box: [276, 548, 700, 900], c: "tl" },
      { box: [1261, 496, 1568, 556], c: "tr" },
      { box: [1261, 568, 1568, 608], c: "br" }
    ]
  },
  planFill: {
    src: "plan-fill.png", crop: [330, 236, 600, 440], width: 330,
    caption: "The same colours on the floor plan.",
    callouts: [
      { box: [334, 241, 786, 260], c: "br" },
      { box: [630, 594, 678, 614], c: "tr" }
    ]
  },
  catLegend: {
    src: "colours-category.jpg", crop: [0, 238, 1470, 48],
    caption: "Colours by category: each container takes its first category's colour, one hue per group.",
    callouts: []
  },
  doors: {
    src: "doors.png", crop: [0, 96, 1000, 210],
    caption: "The Doors tab of Lakeview Manor: the cellar and the two exterior cells around it.",
    callouts: [
      { box: [8, 223, 48, 296], c: "tl" },
      { box: [56, 223, 152, 298], c: "tr" },
      { box: [160, 223, 645, 298], c: "tr" }
    ]
  },
  template: {
    src: "template.png", crop: [0, 182, 1000, 520],
    caption: "The Template tab: details, cells, problems and saving.",
    callouts: [
      { box: [8, 187, 1000, 345], c: "tr" },
      { box: [8, 352, 700, 410], c: "r" },
      { box: [8, 420, 470, 480], c: "r" },
      { box: [8, 490, 330, 528], c: "r" },
      { box: [8, 538, 780, 597], c: "r" },
      { box: [8, 602, 176, 623], c: "r" },
      { box: [8, 628, 1000, 700], c: "tr" }
    ]
  }
};
