// Adobe Illustrator Script: Offset Inset & Grey Fill
// For each selected closed path or compound path:
//   1. Creates a duplicate
//   2. Applies a -1.0 mm offset with round corners to the duplicate
//   3. Expands the offset into real geometry
//   4. Fills the new offset shape with RGB 218, 218, 218 and removes the stroke
//
// The original path is left untouched. The new grey-filled offset sits in the
// same parent layer/group, stacked above the original by default.

function offsetInsetAndFill() {
    if (app.documents.length === 0) return;
    var doc = app.activeDocument;
    if (doc.selection.length === 0) {
        alert("Select one or more closed paths or compound paths.");
        return;
    }

    var items = [];
    for (var s = 0; s < doc.selection.length; s++) items.push(doc.selection[s]);

    // The grey must match the document's colour space, otherwise the
    // assignment fails silently. #dadada (RGB 218,218,218) is a light neutral
    // grey; the CMYK equivalent below is ~14.5% black. (Illustrator documents
    // are only ever RGB or CMYK, so those are the only two cases.)
    var greyFill;
    if (doc.documentColorSpace === DocumentColorSpace.CMYK) {
        greyFill = new CMYKColor();
        greyFill.cyan = 0; greyFill.magenta = 0; greyFill.yellow = 0;
        greyFill.black = 14.5;   // tweak this if you need a different grey
    } else {
        greyFill = new RGBColor();
        greyFill.red = 218; greyFill.green = 218; greyFill.blue = 218;
    }

    var OFFSET_MM = -1.0;
    var offsetPts = OFFSET_MM * 2.83465;  // 1 mm = 2.83465 pt

    // Live Offset Path effect XML.
    // IMPORTANT: the only keys Illustrator recognises are:
    //   mlim  miter limit (real)
    //   ofst  offset distance in points (real, negative = inset)
    //   jntp  join type (integer): 0 = Round, 1 = Bevel, 2 = Miter
    //         (this is the internal enum, NOT the dialog's dropdown order)
    // Any other key name (e.g. "roundness", "offset") is silently ignored and
    // Illustrator falls back to its default ~10 pt OUTWARD offset.
    var effectXml =
        '<LiveEffect name="Adobe Offset Path">' +
        '<Dict data="R mlim 4 R ofst ' + offsetPts + ' I jntp 0 "/>' +
        '</LiveEffect>';

    var processed = 0;

    for (var i = items.length - 1; i >= 0; i--) {
        try { if (processItem(items[i])) processed++; } catch (e) {}
    }

    doc.selection = null;

    if (processed === 0) {
        alert("No applicable closed paths were processed.");
    }

    // ─────────────────────────────────────────────────────────────────────────

    function processItem(item) {
        if (item.typename === "GroupItem") {
            var any = false;
            for (var g = item.pageItems.length - 1; g >= 0; g--) {
                try { if (processItem(item.pageItems[g])) any = true; } catch (e) {}
            }
            return any;
        }
        // Compound paths can be offset directly — no release/re-compound needed.
        if (item.typename === "CompoundPathItem") {
            return offsetAndStyle(item);
        }
        if (item.typename === "PathItem" && item.closed) {
            return offsetAndStyle(item);
        }
        return false;
    }

    function offsetAndStyle(item) {
        // Duplicate — this duplicate becomes the offset shape.
        var dup;
        try { dup = item.duplicate(); } catch (e) { return false; }

        // Style the duplicate grey / no-stroke BEFORE applying the effect.
        // Expand Appearance bakes the current fill/stroke into the new geometry,
        // and doing it here means the result is grey even if the post-expand
        // reference handling below misses (Expand can invalidate `dup`).
        applyGreyFill(dup);

        // Apply the live offset effect to the duplicate.
        try { dup.applyEffect(effectXml); } catch (e) { return false; }

        // Expand appearance (Object > Expand Appearance) to bake the offset
        // into real geometry. Operates on the current selection.
        doc.selection = null;
        try { dup.selected = true; } catch (e) {}
        try { app.executeMenuCommand("expandStyle"); } catch (e) {}

        // Backstop: re-style whatever the expansion produced (a path, a
        // compound, or a group), in case it didn't inherit the fill, and bring
        // it to the front so the grey offset sits directly above the original.
        var results = [];
        for (var k = 0; k < doc.selection.length; k++) results.push(doc.selection[k]);
        for (var r = 0; r < results.length; r++) {
            try { applyGreyFill(results[r]); } catch (e) {}
            try { results[r].move(item, ElementPlacement.PLACEBEFORE); } catch (e) {}
        }

        doc.selection = null;
        return true;
    }

    function applyGreyFill(item) {
        if (item.typename === "GroupItem") {
            for (var i = 0; i < item.pageItems.length; i++) {
                try { applyGreyFill(item.pageItems[i]); } catch (e) {}
            }
        } else if (item.typename === "CompoundPathItem") {
            // A compound path's fill lives on its member sub-paths. Assigning
            // fillColor to the CompoundPathItem itself throws in most Illustrator
            // versions (silently caught), which is why the fill never changed.
            // Set it on each child sub-path instead.
            for (var j = 0; j < item.pathItems.length; j++) {
                try { item.pathItems[j].filled = true; } catch (e) {}
                try { item.pathItems[j].fillColor = greyFill; } catch (e) {}
                try { item.pathItems[j].stroked = false; } catch (e) {}
            }
        } else if (item.typename === "PathItem") {
            try { item.filled = true; } catch (e) {}
            try { item.fillColor = greyFill; } catch (e) {}
            try { item.stroked = false; } catch (e) {}
        }
    }
}

// Run via a deferred BridgeTalk message rather than a direct call.
//
// When this script is triggered from an Action (e.g. bound to a hotkey like
// Ctrl+F3), Illustrator's Action Player still holds its internal menu-command
// dispatch lock while the "Run Script" step executes. The
// app.executeMenuCommand("expandStyle") call above (Object > Expand Appearance,
// which bakes the live offset into real geometry) gets silently dropped or
// behaves inconsistently inside that lock, so the offset never expands and the
// result is wrong. Run from File > Scripts, the same call works fine.
//
// Sending the work as a BridgeTalk message to Illustrator queues it to run on
// the next idle cycle — after the Action Player finishes and releases that
// lock — so executeMenuCommand behaves the same as when run directly.
// BridgeTalk is used instead of app.scheduleTask() because scheduleTask isn't
// available in every Illustrator build (it throws Error 24 on some versions).
//
// offsetInsetAndFill.toString() sends the whole function as source, so the
// deferred run is self-contained and doesn't depend on this script's scope.
// If the message can't be sent for any reason, fall back to a direct call.
var sentViaBridgeTalk = false;
try {
    var bt = new BridgeTalk();
    bt.target = "illustrator";
    bt.body = "(" + offsetInsetAndFill.toString() + ")();";
    bt.onError = function (errObj) {
        alert("Sumoisturize failed to run: " + errObj.body);
    };
    sentViaBridgeTalk = bt.send();
} catch (e) {
    sentViaBridgeTalk = false;
}
if (!sentViaBridgeTalk) offsetInsetAndFill();
