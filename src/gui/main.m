#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (strong) NSWindow *window;
@property (strong) NSTextView *outputView;
@property (strong) NSButton *runButton;
@property (strong) NSButton *benchButton;
@property (strong) NSButton *openButton;
@property (strong) NSTextField *statusLabel;
@property (assign) BOOL isRunning;
@property (strong) NSString *pendingExe;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [self setupWindow];

    NSArray *args = [[NSProcessInfo processInfo] arguments];
    if (args.count > 1) {
        NSString *exePath = args[1];
        if ([[NSFileManager defaultManager] fileExistsAtPath:exePath]) {
            [self runExe:exePath directEntry:nil];
            return;
        }
    }

    if (_pendingExe) {
        NSString *exe = _pendingExe;
        _pendingExe = nil;
        [self runExe:exe directEntry:nil];
        return;
    }

    [self appendOutput:@"=== Rosetta Killer v0.1.0 ===\n"
                 color:[NSColor colorWithRed:0.4 green:0.9 blue:1.0 alpha:1.0]];
    [self appendOutput:@"x86_64 Windows PE → ARM64 macOS via FEXCore JIT\n\n"
                 color:[NSColor colorWithRed:0.7 green:0.7 blue:0.7 alpha:1.0]];
    [self appendOutput:@"  ▶  Run Demo    — basic JIT translation test\n"
                 color:[NSColor colorWithRed:0.0 green:0.9 blue:0.4 alpha:1.0]];
    [self appendOutput:@"  ▶  Benchmark   — Fibonacci, prime sieve, FNV-1a hash\n"
                 color:[NSColor colorWithRed:0.0 green:0.9 blue:0.4 alpha:1.0]];
    [self appendOutput:@"  ▶  Open .exe   — pick any x86_64 Windows PE\n"
                 color:[NSColor colorWithRed:0.0 green:0.9 blue:0.4 alpha:1.0]];
    [self appendOutput:@"  ▶  Drag-and-drop .exe files onto this window\n\n"
                 color:[NSColor colorWithRed:0.0 green:0.9 blue:0.4 alpha:1.0]];
}

- (void)setupWindow {
    NSRect screenFrame = [[NSScreen mainScreen] frame];
    CGFloat w = 720, h = 520;
    CGFloat x = (screenFrame.size.width - w) / 2;
    CGFloat y = (screenFrame.size.height - h) / 2;

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(x, y, w, h)
                                          styleMask:NSWindowStyleMaskTitled |
                                                    NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable |
                                                    NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setTitle:@"Rosetta Killer"];
    [_window setMinSize:NSMakeSize(500, 350)];

    NSView *content = [[NSView alloc] initWithFrame:[[_window contentView] bounds]];
    [content setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    // Title
    NSTextField *title = [NSTextField labelWithString:@"Rosetta Killer"];
    [title setFont:[NSFont systemFontOfSize:24 weight:NSFontWeightBold]];
    [title setTextColor:[NSColor labelColor]];
    [title setFrame:NSMakeRect(20, h - 52, 300, 30)];
    [title setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
    [content addSubview:title];

    NSTextField *subtitle = [NSTextField labelWithString:@"x86_64 PE → ARM64 JIT Translation Engine"];
    [subtitle setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightRegular]];
    [subtitle setTextColor:[NSColor secondaryLabelColor]];
    [subtitle setFrame:NSMakeRect(20, h - 75, 400, 20)];
    [subtitle setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
    [content addSubview:subtitle];

    // Status
    _statusLabel = [NSTextField labelWithString:@"● Ready"];
    [_statusLabel setFont:[NSFont systemFontOfSize:12 weight:NSFontWeightMedium]];
    [_statusLabel setTextColor:[NSColor systemGreenColor]];
    [_statusLabel setFrame:NSMakeRect(w - 200, h - 52, 180, 20)];
    [_statusLabel setAlignment:NSTextAlignmentRight];
    [_statusLabel setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [content addSubview:_statusLabel];

    // Buttons
    _runButton = [NSButton buttonWithTitle:@"Run Demo" target:self action:@selector(runDemoClicked:)];
    [_runButton setBezelStyle:NSBezelStyleRounded];
    [_runButton setFrame:NSMakeRect(20, h - 107, 110, 28)];
    [_runButton setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
    [content addSubview:_runButton];

    _benchButton = [NSButton buttonWithTitle:@"Benchmark" target:self action:@selector(benchmarkClicked:)];
    [_benchButton setBezelStyle:NSBezelStyleRounded];
    [_benchButton setFrame:NSMakeRect(140, h - 107, 110, 28)];
    [_benchButton setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
    [content addSubview:_benchButton];

    _openButton = [NSButton buttonWithTitle:@"Open .exe" target:self action:@selector(openExeClicked:)];
    [_openButton setBezelStyle:NSBezelStyleRounded];
    [_openButton setFrame:NSMakeRect(260, h - 107, 110, 28)];
    [_openButton setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];
    [content addSubview:_openButton];

    // Output scroll view
    NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(15, 15, w - 30, h - 130)];
    [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setBorderType:NSBezelBorder];

    _outputView = [[NSTextView alloc] initWithFrame:[[scrollView contentView] bounds]];
    [_outputView setAutoresizingMask:NSViewWidthSizable];
    [_outputView setEditable:NO];
    [_outputView setSelectable:YES];
    [_outputView setFont:[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]];
    [_outputView setBackgroundColor:[NSColor colorWithWhite:0.08 alpha:1.0]];
    [_outputView setTextColor:[NSColor colorWithRed:0.0 green:0.9 blue:0.4 alpha:1.0]];
    [_outputView setTextContainerInset:NSMakeSize(10, 10)];

    [scrollView setDocumentView:_outputView];
    [content addSubview:scrollView];

    // Register drag-and-drop
    [content registerForDraggedTypes:@[NSPasteboardTypeFileURL]];

    [_window setContentView:content];
    [_window makeKeyAndOrderFront:nil];
}

- (void)appendOutput:(NSString *)text color:(NSColor *)color {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSDictionary *attrs = @{
            NSFontAttributeName: [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: color
        };
        NSAttributedString *attrStr = [[NSAttributedString alloc] initWithString:text attributes:attrs];
        [[_outputView textStorage] appendAttributedString:attrStr];
        [_outputView scrollToEndOfDocument:nil];
    });
}

- (void)appendStdout:(NSString *)text {
    [self appendOutput:text color:[NSColor colorWithRed:0.0 green:0.9 blue:0.4 alpha:1.0]];
}

- (void)appendStderr:(NSString *)text {
    [self appendOutput:text color:[NSColor colorWithRed:0.9 green:0.6 blue:0.2 alpha:1.0]];
}

- (IBAction)runDemoClicked:(id)sender {
    NSString *bundle = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents"];
    NSString *demo = [bundle stringByAppendingPathComponent:@"Resources/test_apps/demo_direct.exe"];
    [self runExe:demo directEntry:@"0x1400010d0"];
}

- (IBAction)benchmarkClicked:(id)sender {
    NSString *bundle = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents"];
    NSString *bench = [bundle stringByAppendingPathComponent:@"Resources/test_apps/benchmark.exe"];
    [self runExe:bench directEntry:@"0x1400012b0"];
}

- (IBAction)openExeClicked:(id)sender {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    if (@available(macOS 11.0, *)) {
        [panel setAllowedContentTypes:@[[UTType typeWithFilenameExtension:@"exe"]]];
    }
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select a Windows x86_64 .exe"];

    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = [[panel URLs] firstObject];
        if (url) [self runExe:[url path] directEntry:nil];
    }
}

- (void)runExe:(NSString *)path directEntry:(NSString *)entry {
    if (_isRunning) {
        [self appendOutput:@"Already running. Please wait.\n"
                     color:[NSColor systemRedColor]];
        return;
    }
    _isRunning = YES;
    [_runButton setEnabled:NO];
    [_benchButton setEnabled:NO];
    [_openButton setEnabled:NO];
    [_statusLabel setStringValue:@"● Running..."];
    [_statusLabel setTextColor:[NSColor systemOrangeColor]];

    [_outputView setString:@""];
    [self appendOutput:@"=== Rosetta Killer v0.1.0 ===\n"
                 color:[NSColor colorWithRed:0.4 green:0.9 blue:1.0 alpha:1.0]];
    [self appendOutput:[NSString stringWithFormat:@"Translating: %@\n",
                        [path lastPathComponent]]
                 color:[NSColor colorWithRed:0.7 green:0.7 blue:0.7 alpha:1.0]];
    [self appendOutput:@"x86_64 PE → FEXCore JIT → ARM64 macOS\n\n"
                 color:[NSColor colorWithRed:0.7 green:0.7 blue:0.7 alpha:1.0]];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [self executeWine:path directEntry:entry];
    });
}

- (void)executeWine:(NSString *)exePath directEntry:(NSString *)entry {
    NSString *bundleContents = [[[NSBundle mainBundle] bundlePath]
                                stringByAppendingPathComponent:@"Contents"];
    NSString *wineDir = [bundleContents stringByAppendingPathComponent:@"Resources/wine"];
    NSString *wineBin = [wineDir stringByAppendingPathComponent:@"bin/wine"];

    NSTask *task = [[NSTask alloc] init];
    [task setExecutableURL:[NSURL fileURLWithPath:wineBin]];
    [task setArguments:@[exePath]];

    NSMutableDictionary *env = [[[NSProcessInfo processInfo] environment] mutableCopy];
    env[@"FEXBRIDGE_PATH"] = [bundleContents stringByAppendingPathComponent:
                              @"Frameworks/libfexbridge.dylib"];
    env[@"WINEPREFIX"] = [NSHomeDirectory() stringByAppendingPathComponent:@".rosetta-killer"];
    env[@"WINEDLLPATH"] = [wineDir stringByAppendingPathComponent:@"lib/wine"];
    env[@"WINESERVER"] = [wineDir stringByAppendingPathComponent:@"bin/wineserver"];
    env[@"WINEBOOTSTRAPMODE"] = @"1";
    env[@"WINEDEBUG"] = @"-all";
    env[@"DYLD_LIBRARY_PATH"] = [NSString stringWithFormat:@"%@/lib:%@/Frameworks",
                                 wineDir, bundleContents];
    env[@"PATH"] = [NSString stringWithFormat:@"%@/bin:%@",
                    wineDir, env[@"PATH"] ?: @""];
    if (entry) env[@"FEXBRIDGE_DIRECT"] = entry;
    [task setEnvironment:env];

    NSPipe *stdoutPipe = [NSPipe pipe];
    NSPipe *stderrPipe = [NSPipe pipe];
    [task setStandardOutput:stdoutPipe];
    [task setStandardError:stderrPipe];

    [[stdoutPipe fileHandleForReading] setReadabilityHandler:^(NSFileHandle *handle) {
        NSData *data = [handle availableData];
        if (data.length == 0) return;
        NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (text) [self appendStdout:text];
    }];

    [[stderrPipe fileHandleForReading] setReadabilityHandler:^(NSFileHandle *handle) {
        NSData *data = [handle availableData];
        if (data.length == 0) return;
        NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (text) [self appendStderr:text];
    }];

    NSError *error = nil;
    [task launchAndReturnError:&error];
    if (error) {
        [self appendOutput:[NSString stringWithFormat:@"\nFailed to launch: %@\n",
                            [error localizedDescription]]
                     color:[NSColor systemRedColor]];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_isRunning = NO;
            [self->_runButton setEnabled:YES];
            [self->_openButton setEnabled:YES];
        });
        return;
    }

    [task waitUntilExit];
    [[stdoutPipe fileHandleForReading] setReadabilityHandler:nil];
    [[stderrPipe fileHandleForReading] setReadabilityHandler:nil];

    int exitCode = [task terminationStatus];
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_isRunning = NO;
        [self->_runButton setEnabled:YES];
        [self->_benchButton setEnabled:YES];
        [self->_openButton setEnabled:YES];

        if (exitCode == 0) {
            [self appendOutput:@"\n--- Finished successfully (exit 0) ---\n"
                         color:[NSColor colorWithRed:0.4 green:0.9 blue:1.0 alpha:1.0]];
            [self->_statusLabel setStringValue:@"● Success"];
            [self->_statusLabel setTextColor:[NSColor systemGreenColor]];
        } else {
            [self appendOutput:[NSString stringWithFormat:
                                @"\n--- Exited with code %d ---\n", exitCode]
                         color:[NSColor systemRedColor]];
            [self->_statusLabel setStringValue:
             [NSString stringWithFormat:@"● Exit: %d", exitCode]];
            [self->_statusLabel setTextColor:[NSColor systemRedColor]];
        }
    });
}

- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename {
    if ([filename hasSuffix:@".exe"]) {
        if (_window) {
            [self runExe:filename directEntry:nil];
        } else {
            _pendingExe = filename;
        }
        return YES;
    }
    return NO;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];

        // Create basic menu
        NSMenu *menuBar = [[NSMenu alloc] init];
        NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:appMenuItem];
        NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Rosetta Killer"];
        [appMenu addItemWithTitle:@"About Rosetta Killer"
                           action:@selector(orderFrontStandardAboutPanel:)
                    keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit Rosetta Killer"
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];
        [appMenuItem setSubmenu:appMenu];
        [app setMainMenu:menuBar];

        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
