/* Screenshots and their callouts. Boxes are in the screenshot's own pixels (1584 x 911 captures of the old
   window; the one-workspace retakes are 1600 x 900, --window 1600x900, see ../README.md for the commands);
   figures with callouts: [] wait for their retake; crop = [x, y, width, height]; width = display px.
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
    caption: "The window before a home is open: the homes rail, the start panel and the inspector.",
    callouts: []
  },
  mods: {
    src: "homes-by-mod.png", crop: [0, 0, 600, 272],
    caption: "The homes rail: every home of your load order under its mod. A name or Nexus id finds it.",
    callouts: []
  },
  view3dBar: {
    src: "view3d-overview.jpg", crop: [0, 150, 1060, 116],
    caption: "The viewport's floating toolbar: Select, Paint, Layers, Colours, Camera.",
    callouts: []
  },
  view3d: {
    src: "view3d-overview.jpg",
    caption: "A home open in the 3D view (Whitepeak Tower, from above).",
    callouts: []
  },
  plan: {
    src: "floor-plan.png", crop: [0, 180, 1250, 731],
    caption: "The floor plan: the same home seen from above, north up.",
    callouts: []
  },
  select3d: {
    src: "view3d-select.jpg", crop: [60, 380, 1080, 420],
    caption: "Selected in 3D: the potion basket gets a yellow frame and the Selection panel opens on the right.",
    callouts: []
  },
  rolePane: {
    src: "view3d-select.jpg", crop: [1253, 470, 324, 300], width: 300,
    caption: "A container's role in the Selection panel.",
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
    caption: "The drawer's Containers tab lists every container with its role; click a row to select it.",
    callouts: [
      { box: [378, 228, 748, 480], c: "tr" },
      { box: [752, 228, 1122, 480], c: "tr" }
    ]
  },
  paint: {
    src: "paint.jpg", crop: [0, 234, 1250, 670],
    caption: "Paint mode with alchemy.potions picked: the paint bar takes the toolbar's place. Colours switch to “by category” while you paint.",
    callouts: []
  },
  multi3d: {
    src: "multi-select.jpg", crop: [290, 330, 500, 560],
    caption: "Three sacks selected at once.",
    callouts: [ { box: [310, 376, 752, 846], c: "tr" } ]
  },
  multiPane: {
    src: "multi-select.jpg", crop: [1253, 270, 324, 360],
    caption: "The Selection panel edits them together.",
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
    caption: "The Draft panel for Aldamar: its master and each container's suggestions.",
    callouts: []
  },
  draftsList: {
    src: "drafts.png", crop: [10, 92, 526, 470],
    caption: "The homes rail after Draft all homes: a drafted chip per home and the Save N drafts… banner.",
    callouts: []
  },
  draftsCells: {
    src: "drafts-multicell.png", crop: [550, 96, 1030, 330],
    caption: "Lakeview Manor's Draft panel: one home over two cells, compared with its shipped template.",
    callouts: []
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
    caption: "The drawer's Doors tab for Lakeview Manor: the cellar and the two exterior cells around it.",
    callouts: [
      { box: [8, 223, 48, 296], c: "tl" },
      { box: [56, 223, 152, 298], c: "tr" },
      { box: [160, 223, 645, 298], c: "tr" }
    ]
  },
  template: {
    src: "template.png", crop: [0, 182, 1000, 520],
    caption: "The Template panel: details, cells, keep rules and saving.",
    callouts: []
  }
};
