// ============================================================================
// ScriptExecutorMac.mm
// macOS: dock folder location + running scripts by asking Illustrator to run
// the file through AppleScript (`do javascript <file>`), off the main thread.
// The first run may show a macOS "allow to control Adobe Illustrator" prompt.
// ============================================================================

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <dlfcn.h>

#include "ScriptExecutor.h"
#include "LaserScriptDockID.h"

namespace {

NSString* ToNS(const std::string& utf8)
{
    return [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
}

std::string FromNS(NSString* s)
{
    return s ? std::string([s UTF8String]) : std::string();
}

void AnchorSymbol() {}

// Contents/Resources of LaserScriptDock.aip
NSString* PluginResourcesPath()
{
    NSBundle* bundle = [NSBundle bundleWithIdentifier:@"com.laserscriptdock.plugin"];
    if (bundle && bundle.resourcePath)
        return bundle.resourcePath;

    Dl_info info {};
    if (dladdr(reinterpret_cast<const void*>(&AnchorSymbol), &info) && info.dli_fname)
    {
        // LaserScriptDock.aip/Contents/MacOS/LaserScriptDock -> .../Contents/Resources
        NSString* binary = [NSString stringWithUTF8String:info.dli_fname];
        return [[[binary stringByDeletingLastPathComponent] stringByDeletingLastPathComponent]
                   stringByAppendingPathComponent:@"Resources"];
    }
    return nil;
}

} // namespace

namespace ScriptExecutor {

std::string GetDockFolder()
{
    NSArray<NSString*>* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString* base = docs.firstObject ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Documents"];
    NSString* folder = [base stringByAppendingPathComponent:@kLaserScriptDockFolderName];
    [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
    return FromNS(folder);
}

std::string GetSeedFolder()
{
    NSString* res = PluginResourcesPath();
    if (!res)
        return "";
    return FromNS([res stringByAppendingPathComponent:@kLaserScriptDockSeedFolderName]);
}

bool RunScriptAsync(const std::string& scriptPathUtf8, CompletionCallback onDone, std::string& errorOut)
{
    NSString* path = ToNS(scriptPathUtf8);
    BOOL isDir = NO;
    if (path.length == 0 || ![[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDir] || isDir)
    {
        errorOut = "script file not found - press Refresh";
        return false;
    }

    // Off the main thread: Illustrator must stay free to receive the request.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSTask* task = [[NSTask alloc] init];
            task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/osascript"];
            task.arguments = @[
                @"-e", @"on run argv",
                @"-e", @"set scriptFile to POSIX file (item 1 of argv)",
                @"-e", @"with timeout of 86400 seconds",
                @"-e", @"tell application id \"com.adobe.illustrator\" to do javascript scriptFile",
                @"-e", @"end timeout",
                @"-e", @"end run",
                path
            ];

            NSPipe* errPipe = [NSPipe pipe];
            task.standardError = errPipe;
            task.standardOutput = [NSFileHandle fileHandleWithNullDevice];

            NSError* launchError = nil;
            bool ok = false;
            std::string message;

            if ([task launchAndReturnError:&launchError])
            {
                NSData* errData = [[errPipe fileHandleForReading] readDataToEndOfFile];
                [task waitUntilExit];
                ok = (task.terminationStatus == 0);
                if (!ok)
                {
                    NSString* text = [[NSString alloc] initWithData:errData encoding:NSUTF8StringEncoding];
                    text = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    message = text.length ? FromNS(text) : "the script stopped with an error.";
                }
            }
            else
            {
                message = launchError ? FromNS(launchError.localizedDescription) : "couldn't start osascript.";
            }

            if (onDone)
            {
                dispatch_async(dispatch_get_main_queue(), ^{
                    onDone(ok, message);
                });
            }
        }
    });

    return true;
}

void OpenInShell(const std::string& pathUtf8)
{
    NSString* path = ToNS(pathUtf8);
    if (path.length == 0)
        return;

    BOOL isDir = NO;
    [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDir];
    if (isDir)
    {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path isDirectory:YES]];
        return;
    }

    // Files (dock.json): open in the default text editor.
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"];
    task.arguments = @[ @"-t", path ];
    [task launchAndReturnError:nil];
}

} // namespace ScriptExecutor

#endif // __APPLE__
