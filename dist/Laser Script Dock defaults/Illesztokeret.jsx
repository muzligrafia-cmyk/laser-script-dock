// Illesztokeret.jsx  (v2)
// Creates a registration frame ("Illesztőkeret") on the active artboard,
// on its own layer named "Illesztőkeret":
//   - 1 pt black stroked rectangle, inset 10 mm from every artboard edge
//     (stays a live stroke, no fill)
//   - two 7 x 7 mm black squares in the frame's top-left and top-right corners
//   - "Print neve ide" label (Arial 24 pt, right aligned) in the bottom-right,
//     3 mm in from the frame
// Colour follows DompiPoints: #000000 in RGB documents,
// C60 M40 Y40 K100 rich black in CMYK documents.
// All frames share one "Illesztőkeret" layer (v3). The script only stops
// and warns if the ACTIVE artboard already has a frame on it, so every
// artboard can get its own.
//
// DompiPoints.jsx (v6+) recognises this layer: dots may go INSIDE the frame
// (min. 5 mm from the stroke), and keep 10 mm from the squares and the label.
//
// Run in Adobe Illustrator: File > Scripts > Other Script...

#target illustrator

(function () {

    if (app.documents.length === 0) {
        alert("Open a document first.");
        return;
    }

    var doc = app.activeDocument;

    // --- Settings ---------------------------------------------------------
    var LAYER_NAME       = "Illeszt\u0151keret";   // "Illesztőkeret"
    var MARGIN_MM        = 10;
    var STROKE_WEIGHT_PT = 1;
    var SQUARE_MM        = 7;
    var TEXT_MARGIN_MM   = 3;
    var FONT_SIZE_PT     = 24;
    var TEXT_CONTENT     = "Print neve ide";

    var MM_TO_PT = 72 / 25.4;
    var marginPt = MARGIN_MM * MM_TO_PT;

    // --- Active artboard bounds -------------------------------------------
    var abIndex = doc.artboards.getActiveArtboardIndex();
    var abRect  = doc.artboards[abIndex].artboardRect; // [left, top, right, bottom]

    var left   = abRect[0];
    var top    = abRect[1];
    var right  = abRect[2];
    var bottom = abRect[3];

    var rectWidth  = (right - left) - (marginPt * 2);
    var rectHeight = (top - bottom) - (marginPt * 2);

    if (rectWidth <= 0 || rectHeight <= 0) {
        alert("Artboard is too small for a " + MARGIN_MM + " mm margin on all sides.");
        return;
    }

    var rectLeft   = left + marginPt;
    var rectTop    = top - marginPt;
    var rectRight  = rectLeft + rectWidth;
    var rectBottom = rectTop - rectHeight;

    // --- Get or create the layer ------------------------------------------
    // One shared "Illesztőkeret" layer for all artboards. The script only
    // stops if something on that layer already sits on the ACTIVE artboard.
    var abName = doc.artboards[abIndex].name;
    var layer = null;
    try { layer = doc.layers.getByName(LAYER_NAME); } catch (e) { layer = null; }

    function overlapsArtboard(gb) { // both are [left, top, right, bottom]
        return !(gb[2] <= left || gb[0] >= right || gb[1] <= bottom || gb[3] >= top);
    }

    var wasLocked = false, wasHidden = false;
    if (layer !== null) {
        for (var k = 0; k < layer.pageItems.length; k++) {
            var gb;
            try { gb = layer.pageItems[k].geometricBounds; } catch (e) { continue; }
            if (overlapsArtboard(gb)) {
                alert("Artboard '" + abName + "' (#" + (abIndex + 1) + ") already has an " +
                      "Illeszt\u0151keret frame.\n\nNothing was added.");
                return;
            }
        }
        // a locked or hidden layer can't receive new objects: open it up
        // for now, restore it at the end
        wasLocked = layer.locked;
        wasHidden = !layer.visible;
        layer.locked  = false;
        layer.visible = true;
    } else {
        layer = doc.layers.add();
        layer.name = LAYER_NAME;
    }

    // same logic as DompiPoints: #000000 in RGB docs, rich black in CMYK docs
    var black;
    if (doc.documentColorSpace === DocumentColorSpace.RGB) {
        black = new RGBColor();
        black.red = 0; black.green = 0; black.blue = 0;                        // #000000
    } else {
        black = new CMYKColor();
        black.cyan = 60; black.magenta = 40; black.yellow = 40; black.black = 100; // rich black
    }

    // --- Frame (live 1 pt stroke) -----------------------------------------
    var frame = layer.pathItems.rectangle(rectTop, rectLeft, rectWidth, rectHeight);
    frame.filled      = false;
    frame.stroked     = true;
    frame.strokeWidth = STROKE_WEIGHT_PT;
    frame.strokeColor = black;
    frame.name        = "Illesztokeret Frame";

    // --- Corner squares ---------------------------------------------------
    var squarePt = SQUARE_MM * MM_TO_PT;

    function makeBlackSquare(topLeftX, topLeftY, name) {
        var sq = layer.pathItems.rectangle(topLeftY, topLeftX, squarePt, squarePt);
        sq.stroked   = false;
        sq.filled    = true;
        sq.fillColor = black;
        sq.name      = name;
        return sq;
    }

    makeBlackSquare(rectLeft,             rectTop, "Corner Square TL");
    makeBlackSquare(rectRight - squarePt, rectTop, "Corner Square TR");

    // --- Bottom-right label -----------------------------------------------
    var textMarginPt = TEXT_MARGIN_MM * MM_TO_PT;
    var textFrame = layer.textFrames.pointText([rectRight - textMarginPt, rectBottom + textMarginPt]);
    textFrame.contents = TEXT_CONTENT;
    textFrame.name     = "Bottom Right Label";

    var textRange = textFrame.textRange;
    var charAttrs = textRange.characterAttributes;
    charAttrs.size      = FONT_SIZE_PT;
    charAttrs.fillColor = black;

    // getByName() wants the PostScript name, so search by family/style instead
    function findFont(family, style) {
        for (var i = 0; i < app.textFonts.length; i++) {
            var f = app.textFonts[i];
            if (f.family === family && f.style === style) return f;
        }
        return null;
    }

    var arialRegular = findFont("Arial", "Regular");
    if (arialRegular) {
        charAttrs.textFont = arialRegular;
    } else {
        alert("Arial was not found on this system; the label was created with the default font instead.");
    }

    // right aligned: text grows leftward, anchor stays on the 3 mm margin
    textRange.paragraphAttributes.justification = Justification.RIGHT;

    // put the layer back the way it was
    if (wasHidden) layer.visible = false;
    if (wasLocked) layer.locked  = true;

    app.redraw();

})();
