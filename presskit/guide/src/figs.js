/* Screenshots and their callouts. Boxes are in the screenshot's own pixels: 1600 x 900 window captures
   (--window 1600x900, see ../README.md for the commands) unless the figure has a size of its own;
   crop = [x, y, width, height]; width = display px (default: the column).
   Callouts are numbered in order (or by n); the legend under each figure uses the same numbers.
   c places the badge: tl/tr/bl/br on a corner of the box, r/l beside it (toolbar rows); at: [x, y] puts it
   there with a leader line. */
window.FIGS = {
  hero: {
    src: "hero.jpg", size: [1600, 760], crop: [0, 0, 1600, 700],
    caption: "Breezehome as the editor draws it, from your own meshes and textures.",
    callouts: []
  },
  start: {
    src: "start.png", width: 600,
    caption: "The window before a home is open: the homes rail, the start panel with the homes in progress, and the inspector.",
    callouts: [
      { box: [4, 4, 1596, 36], c: "tl" },
      { box: [4, 44, 274, 872], c: "tr" },
      { box: [392, 100, 1262, 505], c: "tr" },
      { box: [1270, 44, 1596, 872], c: "tr" },
      { box: [4, 878, 1596, 898], c: "tl" }
    ]
  },
  mods: {
    src: "homes-by-mod.png", size: [1600, 300], crop: [0, 44, 400, 216],
    caption: "The homes rail, searched by Nexus id: the mod, its home and the last scan.",
    callouts: [
      { box: [16, 52, 228, 78], at: [330, 65] },
      { box: [237, 55, 257, 75], at: [330, 95] },
      { box: [16, 88, 262, 104], at: [330, 125] },
      { box: [16, 116, 258, 136], at: [330, 155] },
      { box: [16, 145, 70, 157], at: [330, 185] },
      { box: [16, 240, 172, 254], at: [330, 215] }
    ]
  },
  view3d: {
    src: "view3d-overview.jpg", crop: [280, 0, 1320, 860],
    caption: "A home open in the 3D view (Whitepeak Tower, from above), with the viewport's toolbar: Select, Paint, Layers, Colours, Camera.",
    callouts: [
      { box: [560, 80, 990, 720], c: "tl" },
      { box: [1285, 100, 1583, 250], c: "tr" },
      { box: [286, 824, 745, 840], c: "r" },
      { box: [288, 48, 382, 74], c: "bl" },
      { box: [398, 48, 470, 74], c: "bl" },
      { box: [484, 48, 658, 74], c: "bl" },
      { box: [758, 6, 842, 32], c: "r" }
    ]
  },
  plan: {
    src: "floor-plan.png", crop: [280, 40, 1320, 810],
    caption: "The floor plan: the same home seen from above, north up. The selected satchel is marked on the plan and open in the Selection panel.",
    callouts: [
      { box: [288, 48, 382, 74], c: "bl" },
      { box: [398, 48, 470, 74], c: "bl" },
      { box: [484, 48, 658, 74], c: "bl" },
      { box: [940, 280, 975, 310], c: "tr" },
      { box: [286, 795, 486, 808], c: "tr" },
      { box: [1236, 44, 1252, 60], c: "bl" }
    ]
  },
  select3d: {
    src: "view3d-select.jpg", crop: [500, 88, 1100, 440],
    caption: "Selected in 3D: the potion basket gets a yellow frame and the Selection panel opens on the right, its mesh next to its name.",
    callouts: []
  },
  rolePane: {
    src: "view3d-select.jpg", crop: [1278, 285, 316, 303], width: 294,
    caption: "A container's role in the Selection panel.",
    callouts: [
      { box: [1285, 318, 1357, 341], c: "r" },
      { box: [1285, 350, 1583, 392], c: "tr" },
      { box: [1285, 399, 1345, 416], c: "r" },
      { box: [1285, 423, 1583, 490], c: "tr" },
      { box: [1285, 499, 1583, 560], c: "tr" },
      { box: [1285, 568, 1386, 580], c: "r" }
    ]
  },
  ignored: {
    src: "role-ignored.jpg", crop: [1278, 90, 316, 420], width: 270,
    caption: "A respawning urn: the draft already ignores it, with the reason as its note.",
    callouts: [
      { box: [1388, 137, 1442, 151], c: "r" },
      { box: [1285, 412, 1345, 428], c: "r" },
      { box: [1285, 452, 1583, 498], c: "tr" }
    ]
  },
  containers: {
    src: "containers-table.png", crop: [0, 648, 1600, 228],
    caption: "The drawer's Containers tab lists every container with its role; click a row to select it.",
    callouts: [
      { box: [387, 712, 760, 874], c: "tr" },
      { box: [768, 712, 1138, 874], c: "tr" }
    ]
  },
  paint: {
    src: "paint.jpg", crop: [280, 40, 1320, 810], width: 620,
    caption: "Paint mode with alchemy.potions picked: the paint bar takes the toolbar's place. Colours switch to “by category” while you paint.",
    callouts: [
      { box: [288, 48, 630, 74], c: "bl" },
      { box: [955, 248, 985, 275], c: "tr" },
      { box: [680, 360, 860, 520], c: "tl" },
      { box: [286, 824, 590, 840], c: "r" }
    ]
  },
  multi3d: {
    src: "multi-select.jpg", crop: [370, 170, 560, 620], width: 300,
    caption: "Three sacks selected at once.",
    callouts: [ { box: [380, 180, 920, 780], c: "tl" } ]
  },
  multiPane: {
    src: "multi-select.jpg", crop: [1278, 88, 316, 410], width: 300,
    caption: "The Selection panel edits them together.",
    callouts: [
      { n: 2, box: [1285, 92, 1440, 130], c: "br" },
      { n: 3, box: [1285, 176, 1320, 190], c: "r" },
      { n: 4, box: [1285, 196, 1583, 272], c: "tr" },
      { n: 5, box: [1285, 318, 1395, 343], c: "r" },
      { n: 6, box: [1285, 385, 1583, 456], c: "tr" },
      { n: 7, box: [1285, 477, 1368, 490], c: "r" }
    ]
  },
  mannequin: {
    src: "mannequin.jpg", crop: [280, 80, 1320, 520],
    caption: "A mannequin selected in Whitepeak Tower. The blue see-through boxes are weapon-rack triggers.",
    callouts: [
      { box: [590, 130, 980, 640], c: "tr" },
      { box: [282, 220, 520, 620], c: "tl" },
      { box: [1388, 114, 1462, 130], c: "r" },
      { box: [1285, 342, 1380, 360], c: "r" },
      { box: [1285, 448, 1583, 482], c: "tr" },
      { box: [1285, 490, 1583, 555], c: "tr" }
    ]
  },
  rackKind: {
    src: "rack.jpg", crop: [1278, 88, 316, 102], width: 294,
    caption: "A shield rack: its kind decides what fits.",
    callouts: [ { box: [1388, 114, 1468, 130], c: "r" } ]
  },
  rack: {
    src: "rack.jpg", crop: [1278, 340, 316, 190], width: 294,
    caption: null,
    callouts: [
      { n: 2, box: [1285, 355, 1380, 372], c: "r" },
      { n: 3, box: [1285, 468, 1366, 486], c: "r" },
      { n: 4, box: [1285, 494, 1583, 518], c: "tr" }
    ]
  },
  draftsPane: {
    src: "drafts.png", crop: [1270, 44, 330, 476], width: 294,
    caption: "The Draft panel for Aldamar: its master and each container's suggestions.",
    callouts: [
      { box: [1290, 96, 1530, 114], c: "bl" },
      { box: [1285, 165, 1583, 210], c: "tr" },
      { box: [1285, 363, 1583, 400], c: "tr" },
      { box: [1285, 228, 1440, 242], c: "r" }
    ]
  },
  draftsList: {
    src: "drafts.png", crop: [0, 44, 280, 216], width: 240,
    caption: "The homes rail after Draft all homes: a drafted chip per home and the Save N drafts… banner.",
    callouts: [
      { box: [16, 100, 262, 126], c: "bl" },
      { box: [16, 56, 258, 84], c: "br" },
      { box: [16, 205, 252, 226], c: "bl" }
    ]
  },
  draftsCells: {
    src: "drafts-multicell.png", crop: [1270, 44, 330, 216], width: 330,
    caption: "Lakeview Manor's Draft panel: one home over two cells, compared with its shipped template.",
    callouts: [
      { box: [1285, 124, 1500, 136], c: "br" },
      { box: [1285, 246, 1440, 260], c: "r" }
    ]
  },
  preview: {
    src: "preview.png", crop: [0, 250, 1600, 625],
    caption: "Where items go for Aldamar's draft, with the typical haul, under the home.",
    callouts: [
      { box: [360, 650, 524, 673], c: "bl" },
      { box: [540, 655, 590, 668], c: "bl" },
      { box: [612, 655, 715, 668], c: "bl" },
      { box: [8, 688, 370, 700], c: "tl" },
      { box: [8, 737, 740, 750], c: "tr" },
      { box: [436, 713, 612, 874], c: "tr" },
      { box: [380, 688, 465, 700], c: "br" }
    ]
  },
  fill3d: {
    src: "preview-3d-fill.jpg", crop: [280, 76, 1320, 470],
    caption: "Colours by fill in 3D. The master, a barrel by the door, is selected; the green box beside it gets a few items.",
    callouts: [
      { box: [480, 80, 620, 270], c: "br" },
      { box: [290, 400, 760, 540], c: "tl" },
      { box: [500, 130, 1040, 540], c: "tr" },
      { box: [1285, 385, 1583, 428], c: "tr" }
    ]
  },
  planFill: {
    src: "plan-fill.png", crop: [480, 76, 560, 380], width: 330,
    caption: "The same colours on the floor plan.",
    callouts: [
      { box: [480, 80, 620, 270], c: "br" },
      { box: [795, 410, 845, 432], c: "tr" }
    ]
  },
  catLegend: {
    src: "colours-category.jpg", crop: [478, 176, 700, 130],
    caption: "Colours by category: each container takes its first category's colour, one hue per group.",
    callouts: []
  },
  doors: {
    src: "doors.png", crop: [0, 648, 1600, 150],
    caption: "The drawer's Doors tab for Lakeview Manor: the cellar and the two exterior cells around it.",
    callouts: [
      { box: [1528, 716, 1560, 730], c: "l" },
      { box: [1425, 714, 1445, 780], c: "tl" },
      { box: [8, 714, 840, 780], c: "tr" }
    ]
  },
  template: {
    src: "template.png", crop: [1270, 44, 330, 486], width: 230,
    caption: "The Template panel: details, summary and cells.",
    callouts: [
      { box: [1285, 130, 1570, 352], c: "tr" },
      { box: [1285, 362, 1570, 410], c: "tr" },
      { box: [1285, 430, 1570, 528], c: "tr" }
    ]
  },
  templateSave: {
    src: "template.png", crop: [1270, 660, 330, 150], width: 230,
    caption: "Keep rules, and the Save footer.",
    callouts: [
      { n: 4, box: [1285, 668, 1570, 740], c: "tr" },
      { n: 5, box: [1285, 782, 1470, 806], c: "tr" }
    ]
  }
};
