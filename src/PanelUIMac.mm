// ============================================================================
// PanelUIMac.mm
// macOS panel UI: a scrolling list of script
// buttons grouped under headings, and a status line. Mirrors PanelUIWin.cpp.
// ============================================================================

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "PanelUIController.h"

#include <vector>

namespace {

// Sizes in points
constexpr CGFloat kPad           = 12.0;
constexpr CGFloat kListTop       = 6.0;
constexpr CGFloat kStatusH       = 28.0;
constexpr CGFloat kHeaderH       = 20.0;
constexpr CGFloat kButtonH       = 42.0;
constexpr CGFloat kButtonHNoDesc = 30.0;
constexpr CGFloat kButtonGap     = 6.0;
constexpr CGFloat kCategoryGap   = 8.0;

// Font sizes in points
constexpr CGFloat kFontTool   = 12.0;
constexpr CGFloat kFontHeader = 10.0;
constexpr CGFloat kFontTitle  = 12.0;
constexpr CGFloat kFontDesc   = 10.0;
constexpr CGFloat kFontStatus = 10.5;

constexpr NSInteger kTagRefresh    = -1;
constexpr NSInteger kTagOpenFolder = -2;

NSColor* ColorFromRGB(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0
                               green:((rgb >> 8) & 0xFF) / 255.0
                                blue:(rgb & 0xFF) / 255.0
                               alpha:1.0];
}

NSString* ToNS(const std::string& utf8)
{
    return [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
}

} // namespace

// ----------------------------------------------------------------------------
// A hand-drawn button (tool button or script button)
// ----------------------------------------------------------------------------

@interface LSDButton : NSButton
@property (nonatomic, copy) NSString* titleText;
@property (nonatomic, copy) NSString* descText;
@property (nonatomic, assign) BOOL isTool;
@property (nonatomic, assign) BOOL hovered;
@property (nonatomic, assign) PanelThemeColors theme;
@end

@implementation LSDButton {
    NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.bordered = NO;
        [self setButtonType:NSButtonTypeMomentaryPushIn];
        self.title = @"";
        self.focusRingType = NSFocusRingTypeNone;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_tracking)
        [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                             options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect)
                                               owner:self
                                            userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (void)mouseEntered:(NSEvent*)event { self.hovered = YES; [self setNeedsDisplay:YES]; }
- (void)mouseExited:(NSEvent*)event  { self.hovered = NO;  [self setNeedsDisplay:YES]; }

- (void)drawRect:(NSRect)dirtyRect
{
    const PanelThemeColors t = self.theme;

    uint32_t fill = t.surfaceRGB;
    if ([self.cell isHighlighted])
        fill = t.surfacePressedRGB;
    else if (self.hovered)
        fill = t.surfaceHoverRGB;

    NSBezierPath* shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.5, 0.5) xRadius:4 yRadius:4];
    [ColorFromRGB(fill) setFill];
    [shape fill];
    [ColorFromRGB(self.hovered ? t.accentRGB : t.borderRGB) setStroke];
    shape.lineWidth = 1.0;
    [shape stroke];

    NSMutableParagraphStyle* oneLine = [[NSMutableParagraphStyle alloc] init];
    oneLine.lineBreakMode = NSLineBreakByTruncatingTail;

    const CGFloat w = self.bounds.size.width;
    const CGFloat h = self.bounds.size.height;

    if (self.isTool)
    {
        NSMutableParagraphStyle* centered = [oneLine mutableCopy];
        centered.alignment = NSTextAlignmentCenter;
        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:kFontTool weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: ColorFromRGB(t.textPrimaryRGB),
            NSParagraphStyleAttributeName: centered
        };
        NSSize sz = [self.titleText sizeWithAttributes:attrs];
        [self.titleText drawInRect:NSMakeRect(6, (h - sz.height) / 2.0, w - 12, sz.height) withAttributes:attrs];
        return;
    }

    NSDictionary* titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:kFontTitle weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: ColorFromRGB(t.textPrimaryRGB),
        NSParagraphStyleAttributeName: oneLine
    };

    if (self.descText.length == 0)
    {
        NSSize sz = [self.titleText sizeWithAttributes:titleAttrs];
        [self.titleText drawInRect:NSMakeRect(10, (h - sz.height) / 2.0, w - 20, sz.height) withAttributes:titleAttrs];
        return;
    }

    [self.titleText drawInRect:NSMakeRect(10, 5, w - 20, 16) withAttributes:titleAttrs];

    NSDictionary* descAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:kFontDesc],
        NSForegroundColorAttributeName: ColorFromRGB(t.textMutedRGB),
        NSParagraphStyleAttributeName: oneLine
    };
    [self.descText drawInRect:NSMakeRect(10, h - 5 - 14, w - 20, 14) withAttributes:descAttrs];
}

@end

// ----------------------------------------------------------------------------
// Flipped views: the host (whole panel) and the scrolling list contents
// ----------------------------------------------------------------------------

@interface LSDHeaderItem : NSObject
@property (nonatomic, copy) NSString* text;
@property (nonatomic, assign) NSRect rect;
@end
@implementation LSDHeaderItem
@end

@interface LSDListView : NSView
@property (nonatomic, assign) PanelThemeColors theme;
@property (nonatomic, strong) NSMutableArray<LSDHeaderItem*>* headers;
@property (nonatomic, assign) BOOL showEmptyMessage;
@end

@implementation LSDListView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    [ColorFromRGB(self.theme.backgroundRGB) setFill];
    NSRectFill(dirtyRect);

    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:kFontHeader weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: ColorFromRGB(self.theme.textMutedRGB)
    };

    if (self.showEmptyMessage)
    {
        NSDictionary* msgAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:kFontDesc],
            NSForegroundColorAttributeName: ColorFromRGB(self.theme.textMutedRGB)
        };
        NSString* msg = @"No scripts yet.\n\nPut your .jsx or .js files into the folder "
                        @"(≡ menu › Open Scripts Folder), then choose Refresh Scripts.";
        [msg drawWithRect:NSMakeRect(kPad + 2, 8, self.bounds.size.width - kPad * 2, 200)
                  options:NSStringDrawingUsesLineFragmentOrigin
               attributes:msgAttrs
                  context:nil];
        return;
    }

    for (LSDHeaderItem* h in self.headers)
    {
        NSSize sz = [h.text sizeWithAttributes:attrs];
        NSRect r = h.rect;
        r.origin.y += (r.size.height - sz.height) / 2.0;
        r.size.height = sz.height;
        [h.text drawInRect:r withAttributes:attrs];
    }
}
@end

@interface LSDHostView : NSView
@property (nonatomic, assign) PanelThemeColors theme;
@property (nonatomic, assign) NSRect statusRect;
@property (nonatomic, copy) void (^onLayout)(NSSize size);
@end

@implementation LSDHostView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    [ColorFromRGB(self.theme.backgroundRGB) setFill];
    NSRectFill(dirtyRect);
    [ColorFromRGB(self.theme.borderRGB) setFill];
    NSRectFill(NSMakeRect(self.statusRect.origin.x, self.statusRect.origin.y - 4, self.statusRect.size.width, 1));
}
- (void)resizeSubviewsWithOldSize:(NSSize)oldSize
{
    [super resizeSubviewsWithOldSize:oldSize];
    if (self.onLayout)
        self.onLayout(self.bounds.size);
}
@end

// Receives button clicks and hands them to C++ a moment later, so a Refresh
// can safely rebuild the buttons.
@interface LSDActionTarget : NSObject
@property (nonatomic, copy) void (^onClick)(NSInteger tag);
- (void)clicked:(id)sender;
@end

@implementation LSDActionTarget
- (void)clicked:(id)sender
{
    NSInteger tag = [sender tag];
    void (^handler)(NSInteger) = self.onClick;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (handler)
            handler(tag);
    });
}
@end

// ----------------------------------------------------------------------------
// The controller
// ----------------------------------------------------------------------------

namespace {

class PanelUIMac final : public PanelUIController
{
public:
    ~PanelUIMac() override { Destroy(); }

    bool Create(AIPanelPlatformWindow parentWindow, const PanelThemeColors& theme, const PanelCallbacks& callbacks) override
    {
        NSView* parent = (__bridge NSView*)parentWindow;
        if (!parent)
            return false;

        fTheme = theme;
        fCallbacks = callbacks;

        fHost = [[LSDHostView alloc] initWithFrame:parent.bounds];
        fHost.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        fHost.theme = theme;

        fTarget = [[LSDActionTarget alloc] init];
        PanelCallbacks cb = callbacks;
        // The weak host check stops a late click from reaching a closed panel.
        __weak LSDHostView* weakHost = fHost;
        PanelUIMac* owner = this;
        fTarget.onClick = ^(NSInteger tag) {
            if (!weakHost)
                return; // panel already gone
            if (tag == kTagRefresh)
            {
                if (cb.onRefresh) cb.onRefresh();
            }
            else if (tag == kTagOpenFolder)
            {
                if (cb.onOpenFolder) cb.onOpenFolder();
            }
            else if (tag >= 0 && static_cast<size_t>(tag) < owner->fScriptCount)
            {
                if (cb.onRunScript) cb.onRunScript(static_cast<size_t>(tag));
            }
        };

        fList = [[LSDListView alloc] initWithFrame:NSMakeRect(0, 0, parent.bounds.size.width, 10)];
        fList.theme = theme;
        fList.headers = [NSMutableArray array];

        fScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        fScroll.hasVerticalScroller = YES;
        fScroll.hasHorizontalScroller = NO;
        fScroll.autohidesScrollers = YES;
        fScroll.scrollerStyle = NSScrollerStyleOverlay;
        fScroll.borderType = NSNoBorder;
        fScroll.drawsBackground = NO;
        fScroll.contentView.drawsBackground = NO;
        fScroll.documentView = fList;
        [fHost addSubview:fScroll];

        fStatus = [NSTextField labelWithString:@"Ready"];
        fStatus.font = [NSFont systemFontOfSize:kFontStatus];
        fStatus.lineBreakMode = NSLineBreakByTruncatingTail;
        fStatus.usesSingleLineMode = YES;
        fStatus.textColor = ColorFromRGB(theme.textMutedRGB);
        [fHost addSubview:fStatus];

        fHost.onLayout = ^(NSSize size) {
            owner->LayoutHost(size);
        };

        [parent addSubview:fHost];
        LayoutHost(parent.bounds.size);
        return true;
    }

    void Destroy() override
    {
        if (fHost)
        {
            fHost.onLayout = nil;
            [fHost removeFromSuperview];
        }
        fTarget.onClick = nil;
        fHost = nil;
        fList = nil;
        fScroll = nil;
        fStatus = nil;
        fTarget = nil;
        fScriptButtons.clear();
        fScriptCount = 0;
    }

    void SetModel(const DockModel& model) override
    {
        fModel = model;
        if (!fList)
            return;

        for (LSDButton* b : fScriptButtons)
            [b removeFromSuperview];
        fScriptButtons.clear();

        for (size_t i = 0; i < fModel.scripts.size(); ++i)
        {
            const DockScript& s = fModel.scripts[i];
            LSDButton* b = MakeButton(ToNS(s.name), ToNS(s.description), NO, static_cast<NSInteger>(i));
            [fList addSubview:b];
            fScriptButtons.push_back(b);
        }
        fScriptCount = fScriptButtons.size();

        LayoutList();
    }

    void UpdateTheme(const PanelThemeColors& theme) override
    {
        fTheme = theme;
        if (!fHost)
            return;
        fHost.theme = theme;
        fList.theme = theme;
        fStatus.textColor = fStatusError ? [NSColor systemRedColor] : ColorFromRGB(theme.textMutedRGB);
        for (LSDButton* b : fScriptButtons)
        {
            b.theme = theme;
            [b setNeedsDisplay:YES];
        }
        [fList setNeedsDisplay:YES];
        [fHost setNeedsDisplay:YES];
    }

    void SetStatusText(const std::string& utf8Status, bool isError) override
    {
        NSString* text = ToNS(utf8Status);
        __weak NSTextField* weakStatus = fStatus;
        const uint32_t muted = fTheme.textMutedRGB;
        PanelUIMac* owner = this;
        __weak LSDHostView* weakHost = fHost;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSTextField* status = weakStatus;
            if (!status || !weakHost)
                return;
            owner->fStatusError = isError;
            status.stringValue = text;
            status.toolTip = text;
            status.textColor = isError ? [NSColor systemRedColor] : ColorFromRGB(muted);
        });
    }

    void Resize(int width, int height) override
    {
        if (!fHost)
            return;
        fHost.frame = NSMakeRect(0, 0, width, height);
        LayoutHost(NSMakeSize(width, height));
    }

private:
    LSDButton* MakeButton(NSString* title, NSString* desc, BOOL isTool, NSInteger tag)
    {
        LSDButton* b = [[LSDButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 40)];
        b.titleText = title;
        b.descText = desc;
        b.isTool = isTool;
        b.theme = fTheme;
        b.tag = tag;
        b.toolTip = desc.length ? desc : nil;
        b.target = fTarget;
        b.action = @selector(clicked:);
        return b;
    }

    void LayoutHost(NSSize size)
    {
        if (!fHost)
            return;

        const CGFloat innerW = MAX(80.0, size.width - kPad * 2);
        const CGFloat listTop = kListTop;
        const CGFloat statusTop = MAX(listTop + 40.0, size.height - kStatusH);
        fScroll.frame = NSMakeRect(0, listTop, size.width, MAX(0.0, statusTop - listTop));

        fHost.statusRect = NSMakeRect(kPad, statusTop + 4, innerW, size.height - statusTop - 6);
        NSSize statusSize = [fStatus intrinsicContentSize];
        fStatus.frame = NSMakeRect(kPad, statusTop + 4 + MAX(0.0, (fHost.statusRect.size.height - statusSize.height) / 2.0),
                                   innerW, statusSize.height);

        LayoutList();
        [fHost setNeedsDisplay:YES];
    }

    void LayoutList()
    {
        if (!fList || !fScroll)
            return;

        const CGFloat width = fScroll.contentSize.width;
        const CGFloat innerW = MAX(80.0, width - kPad * 2);

        [fList.headers removeAllObjects];
        CGFloat y = 2.0;
        for (const auto& cat : fModel.categories)
        {
            LSDHeaderItem* h = [[LSDHeaderItem alloc] init];
            h.text = [ToNS(cat.name) uppercaseString];
            h.rect = NSMakeRect(kPad + 2, y, innerW, kHeaderH);
            [fList.headers addObject:h];
            y += kHeaderH;

            for (size_t idx : cat.scripts)
            {
                if (idx >= fScriptButtons.size())
                    continue;
                const bool hasDesc = !fModel.scripts[idx].description.empty();
                const CGFloat h2 = hasDesc ? kButtonH : kButtonHNoDesc;
                fScriptButtons[idx].frame = NSMakeRect(kPad, y, innerW, h2);
                y += h2 + kButtonGap;
            }
            y += kCategoryGap;
        }

        fList.showEmptyMessage = fModel.scripts.empty();
        const CGFloat contentH = MAX(y, fScroll.contentSize.height);
        fList.frame = NSMakeRect(0, 0, width, contentH);
        [fList setNeedsDisplay:YES];
    }

    PanelThemeColors        fTheme {};
    PanelCallbacks          fCallbacks;
    DockModel               fModel;
    bool                    fStatusError = false;
    size_t                  fScriptCount = 0;

    LSDHostView*            fHost = nil;
    LSDListView*            fList = nil;
    NSScrollView*           fScroll = nil;
    NSTextField*            fStatus = nil;
    LSDActionTarget*        fTarget = nil;
    std::vector<LSDButton*> fScriptButtons;
};

} // namespace

std::shared_ptr<PanelUIController> PanelUIController::CreatePlatformController()
{
    return std::make_shared<PanelUIMac>();
}

#endif // __APPLE__
