import Cocoa

@main
class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var outputView: NSTextView!
    var runButton: NSButton!
    var stopButton: NSButton!
    var openButton: NSButton!
    var statusLabel: NSTextField!
    var pathField: NSTextField!
    var recentMenu: NSPopUpButton!
    var isRunning = false
    var currentProcess: Process?

    static let recentAppsKey = "RecentApps"
    static let maxRecent = 10
    static let prefixDir = NSHomeDirectory() + "/.fex-for-mac"

    static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.regular)
        let delegate = AppDelegate()
        app.delegate = delegate
        app.run()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.activate(ignoringOtherApps: true)
        migrateOldPrefix()
        setupMenuBar()
        setupWindow()

        if let pending = pendingOpenFile {
            pendingOpenFile = nil
            pathField.stringValue = pending
            runExe(path: pending)
            return
        }

        let args = ProcessInfo.processInfo.arguments
        if args.count > 1 {
            let exePath = args[1]
            if FileManager.default.fileExists(atPath: exePath) {
                pathField.stringValue = exePath
                runExe(path: exePath)
                return
            }
        }

        appendOutput("FEX for Mac v0.5.0 beta (32-bit Windows programs)\n")
        appendOutput("x86_64/i386 Windows PE \u{2192} ARM64 macOS via Wine WoW64 + FEXCore JIT\n")
        appendOutput("RPG Maker games run natively via mkxp-z + ANGLE Metal\n\n")
        appendOutput("Drop a .exe here, browse with \"Open\", or pick a recent app.\n")
    }

    func setupMenuBar() {
        let mainMenu = NSMenu()

        let appMenuItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "About FEX for Mac", action: #selector(showAbout), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit FEX for Mac", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appMenuItem.submenu = appMenu
        mainMenu.addItem(appMenuItem)

        let fileMenuItem = NSMenuItem()
        let fileMenu = NSMenu(title: "File")
        fileMenu.addItem(withTitle: "Open .exe...", action: #selector(openExeClicked), keyEquivalent: "o")
        fileMenu.addItem(withTitle: "Launch Steam", action: #selector(launchSteamClicked), keyEquivalent: "l")
        fileMenu.addItem(.separator())
        fileMenu.addItem(withTitle: "Clear Recent", action: #selector(clearRecent), keyEquivalent: "")
        fileMenuItem.submenu = fileMenu
        mainMenu.addItem(fileMenuItem)

        NSApplication.shared.mainMenu = mainMenu
    }

    func setupWindow() {
        let screenFrame = NSScreen.main?.frame ?? NSRect(x: 0, y: 0, width: 1280, height: 800)
        let windowWidth: CGFloat = 750
        let windowHeight: CGFloat = 550
        let windowX = (screenFrame.width - windowWidth) / 2
        let windowY = (screenFrame.height - windowHeight) / 2

        window = NSWindow(
            contentRect: NSRect(x: windowX, y: windowY, width: windowWidth, height: windowHeight),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "FEX for Mac"
        window.minSize = NSSize(width: 550, height: 400)

        let contentView = NSView(frame: window.contentView!.bounds)
        contentView.autoresizingMask = [.width, .height]

        // Header bar
        let titleLabel = NSTextField(labelWithString: "FEX for Mac")
        titleLabel.font = NSFont.systemFont(ofSize: 22, weight: .bold)
        titleLabel.textColor = .labelColor
        titleLabel.frame = NSRect(x: 20, y: windowHeight - 50, width: 250, height: 28)
        titleLabel.autoresizingMask = [.maxXMargin, .minYMargin]
        contentView.addSubview(titleLabel)

        let subtitleLabel = NSTextField(labelWithString: "Wine WoW64  \u{2022}  FEXCore JIT  \u{2022}  DXMT Metal")
        subtitleLabel.font = NSFont.systemFont(ofSize: 11, weight: .regular)
        subtitleLabel.textColor = .tertiaryLabelColor
        subtitleLabel.frame = NSRect(x: 20, y: windowHeight - 70, width: 400, height: 16)
        subtitleLabel.autoresizingMask = [.maxXMargin, .minYMargin]
        contentView.addSubview(subtitleLabel)

        statusLabel = NSTextField(labelWithString: "Ready")
        statusLabel.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .medium)
        statusLabel.textColor = NSColor.systemGreen
        statusLabel.frame = NSRect(x: windowWidth - 160, y: windowHeight - 50, width: 140, height: 18)
        statusLabel.alignment = .right
        statusLabel.autoresizingMask = [.minXMargin, .minYMargin]
        contentView.addSubview(statusLabel)

        // Separator
        let sep = NSBox(frame: NSRect(x: 15, y: windowHeight - 80, width: windowWidth - 30, height: 1))
        sep.boxType = .separator
        sep.autoresizingMask = [.width, .minYMargin]
        contentView.addSubview(sep)

        // Path row
        let pathLabel = NSTextField(labelWithString: "Executable:")
        pathLabel.font = NSFont.systemFont(ofSize: 12, weight: .medium)
        pathLabel.textColor = .secondaryLabelColor
        pathLabel.frame = NSRect(x: 20, y: windowHeight - 108, width: 80, height: 18)
        pathLabel.autoresizingMask = [.maxXMargin, .minYMargin]
        contentView.addSubview(pathLabel)

        pathField = NSTextField(frame: NSRect(x: 102, y: windowHeight - 112, width: windowWidth - 222, height: 24))
        pathField.placeholderString = "Drop .exe here or click Open..."
        pathField.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        pathField.isEditable = true
        pathField.isBordered = true
        pathField.bezelStyle = .roundedBezel
        pathField.autoresizingMask = [.width, .minYMargin]
        contentView.addSubview(pathField)

        openButton = NSButton(title: "Open...", target: self, action: #selector(openExeClicked))
        openButton.bezelStyle = .rounded
        openButton.frame = NSRect(x: windowWidth - 110, y: windowHeight - 114, width: 90, height: 28)
        openButton.autoresizingMask = [.minXMargin, .minYMargin]
        contentView.addSubview(openButton)

        // Action row
        runButton = NSButton(title: "\u{25B6}  Run", target: self, action: #selector(runClicked))
        runButton.bezelStyle = .rounded
        runButton.frame = NSRect(x: 20, y: windowHeight - 148, width: 90, height: 28)
        runButton.keyEquivalent = "\r"
        runButton.autoresizingMask = [.maxXMargin, .minYMargin]
        contentView.addSubview(runButton)

        stopButton = NSButton(title: "\u{25A0}  Stop", target: self, action: #selector(stopClicked))
        stopButton.bezelStyle = .rounded
        stopButton.frame = NSRect(x: 118, y: windowHeight - 148, width: 90, height: 28)
        stopButton.isEnabled = false
        stopButton.autoresizingMask = [.maxXMargin, .minYMargin]
        contentView.addSubview(stopButton)

        let steamButton = NSButton(title: "Steam", target: self, action: #selector(launchSteamClicked))
        steamButton.bezelStyle = .rounded
        steamButton.frame = NSRect(x: 432, y: windowHeight - 148, width: 90, height: 28)
        steamButton.autoresizingMask = [.maxXMargin, .minYMargin]
        contentView.addSubview(steamButton)

        // Recent apps dropdown
        recentMenu = NSPopUpButton(frame: NSRect(x: 220, y: windowHeight - 148, width: 200, height: 28))
        recentMenu.autoresizingMask = [.width, .minYMargin]
        recentMenu.target = self
        recentMenu.action = #selector(recentSelected)
        contentView.addSubview(recentMenu)
        refreshRecentMenu()

        // Output console
        let scrollView = NSScrollView(frame: NSRect(x: 15, y: 15, width: windowWidth - 30, height: windowHeight - 175))
        scrollView.autoresizingMask = [.width, .height]
        scrollView.hasVerticalScroller = true
        scrollView.borderType = .bezelBorder

        outputView = NSTextView(frame: scrollView.contentView.bounds)
        outputView.autoresizingMask = [.width]
        outputView.isEditable = false
        outputView.isSelectable = true
        outputView.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        outputView.backgroundColor = NSColor(white: 0.08, alpha: 1.0)
        outputView.textColor = NSColor(red: 0.0, green: 0.85, blue: 0.4, alpha: 1.0)
        outputView.textContainerInset = NSSize(width: 8, height: 8)
        scrollView.documentView = outputView
        contentView.addSubview(scrollView)

        // Drag-and-drop overlay
        let dropView = DropView(frame: contentView.bounds, delegate: self)
        dropView.autoresizingMask = [.width, .height]
        contentView.addSubview(dropView, positioned: .above, relativeTo: nil)

        window.contentView = contentView
        window.makeKeyAndOrderFront(nil)
    }

    // MARK: - Output

    func appendOutput(_ text: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            let attrStr = NSAttributedString(string: text, attributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 11, weight: .regular),
                .foregroundColor: NSColor(red: 0.0, green: 0.85, blue: 0.4, alpha: 1.0)
            ])
            self.outputView.textStorage?.append(attrStr)
            self.outputView.scrollToEndOfDocument(nil)
        }
    }

    func appendError(_ text: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            let attrStr = NSAttributedString(string: text, attributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 11, weight: .regular),
                .foregroundColor: NSColor.systemRed
            ])
            self.outputView.textStorage?.append(attrStr)
            self.outputView.scrollToEndOfDocument(nil)
        }
    }

    func appendInfo(_ text: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            let attrStr = NSAttributedString(string: text, attributes: [
                .font: NSFont.monospacedSystemFont(ofSize: 11, weight: .regular),
                .foregroundColor: NSColor.systemYellow
            ])
            self.outputView.textStorage?.append(attrStr)
            self.outputView.scrollToEndOfDocument(nil)
        }
    }

    // MARK: - Actions

    @objc func runClicked() {
        let path = pathField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !path.isEmpty else {
            appendError("No executable selected. Use Open or drop a .exe file.\n")
            return
        }
        guard FileManager.default.fileExists(atPath: path) else {
            appendError("File not found: \(path)\n")
            return
        }
        runExe(path: path)
    }

    /// Starts the Windows Steam client installed in the prefix (fex-run applies the Steam profile: flags, restart on freeze).
    @objc func launchSteamClicked() {
        let steamExe = Self.prefixDir + "/drive_c/Program Files/Steam/steam.exe"
        guard FileManager.default.fileExists(atPath: steamExe) else {
            let alert = NSAlert()
            alert.messageText = "Steam is not installed yet"
            alert.informativeText = "Install it by opening SteamSetup.exe with this app (File \u{2192} Open .exe...). After that, the Steam button starts it."
            alert.runModal()
            return
        }
        pathField.stringValue = steamExe
        runExe(path: steamExe)
    }

    @objc func stopClicked() {
        guard isRunning else { return }
        appendInfo("\nStopping...\n")
        currentProcess?.terminate()
        let wineDir = Bundle.main.bundlePath + "/Contents/Resources/wine"
        if FileManager.default.fileExists(atPath: wineDir + "/bin/wineserver") {
            if FileManager.default.isExecutableFile(atPath: wineDir + "/../bin/fex-run") {
                // fex-run asks the program to quit by itself first (Steam saves its login that way) and stops
                // the Wine server when done; only force it here if that takes too long.
                DispatchQueue.main.asyncAfter(deadline: .now() + 60) { [weak self] in
                    if self?.isRunning == true { self?.killWineserver() }
                }
            } else {
                killWineserver()
            }
        }
    }

    @objc func openExeClicked() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.init(filenameExtension: "exe")!]
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.title = "Select a Windows .exe"

        if panel.runModal() == .OK, let url = panel.url {
            pathField.stringValue = url.path
        }
    }

    @objc func recentSelected() {
        guard recentMenu.indexOfSelectedItem > 0 else { return }
        let path = recentMenu.titleOfSelectedItem ?? ""
        if !path.isEmpty && path != "Recent Apps..." {
            pathField.stringValue = path
        }
    }

    @objc func clearRecent() {
        UserDefaults.standard.removeObject(forKey: Self.recentAppsKey)
        refreshRecentMenu()
    }

    @objc func showAbout() {
        let alert = NSAlert()
        alert.messageText = "FEX for Mac v0.5.0 beta"
        alert.informativeText = """
        32-bit Windows programs \u{2192} ARM64 macOS (beta; 64-bit x86 is not supported yet)

        Free for non-commercial use (PolyForm Noncommercial 1.0.0).
        Not for sale. Third-party parts keep their own licenses.

        Built on the work of:
        \u{2022} Wine (winehq.org) \u{2014} Windows compatibility layer
        \u{2022} FEX-Emu (fex-emu.com) \u{2014} x86/x86-64 JIT for ARM64
        \u{2022} MetalSharp / VKMT-Wine \u{2014} ARM64 macOS Wine patches,
          WoW64 provider architecture, x18\u{2192}x28 TEB fix
        \u{2022} DXMT \u{2014} D3D11 to Metal translation
        \u{2022} MoltenVK \u{2014} Vulkan to Metal
        \u{2022} mkxp-z \u{2014} Native RPG Maker engine

        No Rosetta 2 required.
        """
        alert.alertStyle = .informational
        alert.runModal()
    }

    // MARK: - Prefix Migration

    func migrateOldPrefix() {
        let oldPath = NSHomeDirectory() + "/.rosetta-killer"
        let fm = FileManager.default
        if fm.fileExists(atPath: oldPath) && !fm.fileExists(atPath: Self.prefixDir) {
            try? fm.moveItem(atPath: oldPath, toPath: Self.prefixDir)
        }
    }

    // MARK: - Recent Apps

    func addToRecent(_ path: String) {
        var recent = UserDefaults.standard.stringArray(forKey: Self.recentAppsKey) ?? []
        recent.removeAll { $0 == path }
        recent.insert(path, at: 0)
        if recent.count > Self.maxRecent { recent = Array(recent.prefix(Self.maxRecent)) }
        UserDefaults.standard.set(recent, forKey: Self.recentAppsKey)
        refreshRecentMenu()
    }

    func refreshRecentMenu() {
        recentMenu.removeAllItems()
        recentMenu.addItem(withTitle: "Recent Apps...")
        let recent = UserDefaults.standard.stringArray(forKey: Self.recentAppsKey) ?? []
        if recent.isEmpty {
            let item = recentMenu.menu!.addItem(withTitle: "(none)", action: nil, keyEquivalent: "")
            item.isEnabled = false
        } else {
            for path in recent {
                recentMenu.addItem(withTitle: path)
            }
        }
    }

    // MARK: - Run

    func runExe(path: String) {
        let path = path.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !isRunning else {
            appendError("Already running. Stop the current app first.\n")
            return
        }
        guard !path.isEmpty else {
            appendError("No executable selected.\n")
            return
        }
        isRunning = true
        runButton.isEnabled = false
        stopButton.isEnabled = true
        openButton.isEnabled = false
        statusLabel.stringValue = "Running"
        statusLabel.textColor = .systemOrange
        pathField.stringValue = path

        addToRecent(path)

        outputView.string = ""
        let name = URL(fileURLWithPath: path).lastPathComponent
        appendOutput("Running: \(name)\n")

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }
            if self.isPyInstallerExe(path: path), let launcher = self.detectPyInstallerLauncher(exePath: path) {
                self.runPyInstallerLauncher(launcher: launcher)
            } else if self.isPyInstallerExe(path: path) {
                self.appendInfo("PyInstaller app detected. Extracting bundle...\n")
                self.extractPyInstallerBundle(exePath: path)
                if let launcher = self.detectPyInstallerLauncher(exePath: path) {
                    self.runPyInstallerLauncher(launcher: launcher)
                } else {
                    self.executeWine(exePath: path)
                }
            } else {
                let gameDir = URL(fileURLWithPath: path).deletingLastPathComponent().path
                if let rgssVersion = self.detectRPGMaker(gameDir: gameDir) {
                    self.launchWithMkxpz(gameDir: gameDir, rgssVersion: rgssVersion)
                } else {
                    self.executeWine(exePath: path)
                }
            }
        }
    }

    // MARK: - PyInstaller launcher detection

    struct PyInstallerLauncher {
        let exePath: String
        let meiDir: String
        let games: [(key: String, label: String, repo: String, branch: String, folder: String, exe: String)]
    }

    func isPyInstallerExe(path: String) -> Bool {
        guard let data = FileManager.default.contents(atPath: path),
              data.count > 1024 else { return false }
        let searchRange = data.suffix(min(data.count, 8192))
        let marker = "PYINSTALLER".data(using: .ascii)!
        return searchRange.range(of: marker) != nil
    }

    func extractPyInstallerBundle(exePath: String) {
        let (_, wineDir, baseEnv) = makeWineEnv()
        _ = initWinePrefix(wineDir: wineDir, env: baseEnv)
        let wineBin = wineDir + "/bin/wine"
        let process = Process()
        process.executableURL = URL(fileURLWithPath: wineBin)
        process.arguments = [exePath]
        process.environment = baseEnv
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            Thread.sleep(forTimeInterval: 5)
            process.terminate()
            process.waitUntilExit()
        } catch { }
    }

    func detectPyInstallerLauncher(exePath: String) -> PyInstallerLauncher? {
        let prefixPath = Self.prefixDir
        let windowsDir = prefixPath + "/drive_c/windows"
        let fm = FileManager.default

        guard let entries = try? fm.contentsOfDirectory(atPath: windowsDir) else { return nil }
        for entry in entries where entry.hasPrefix("_MEI") {
            let meiDir = windowsDir + "/" + entry
            let hasGit = fm.fileExists(atPath: meiDir + "/vendor/git")
            let hasKantoBtn = fm.fileExists(atPath: meiDir + "/resources/btn_kanto.png")
            guard hasGit, hasKantoBtn else { continue }

            return PyInstallerLauncher(
                exePath: exePath,
                meiDir: meiDir,
                games: [
                    (key: "kanto", label: "Pokemon Infinite Fusion 1 (Kanto)",
                     repo: "https://github.com/infinitefusion/infinitefusion-e18.git",
                     branch: "releases", folder: "InfiniteFusion", exe: "Game.exe"),
                    (key: "hoenn", label: "Pokemon Infinite Fusion 2 (Hoenn)",
                     repo: "https://github.com/infinitefusion/infinitefusion-hoenn-public.git",
                     branch: "releases", folder: "InfiniteFusion2", exe: "InfiniteFusion2.exe")
                ]
            )
        }
        return nil
    }

    func runPyInstallerLauncher(launcher: PyInstallerLauncher) {
        let (_, wineDir, baseEnv) = makeWineEnv()
        if !initWinePrefix(wineDir: wineDir, env: baseEnv) {
            DispatchQueue.main.async { [weak self] in
                self?.markStopped(status: "Error", color: .systemRed)
            }
            return
        }

        appendInfo("Detected PyInstaller game launcher.\n")
        appendInfo("GUI rendering is not supported under FEXCore \u{2014} using headless mode.\n\n")

        let prefixPath = Self.prefixDir
        let gamesDir = prefixPath + "/drive_c/users/" + NSUserName() + "/Games"

        let game = launcher.games[0]
        let gameDir = gamesDir + "/" + game.folder
        let gameExe = gameDir + "/" + game.exe

        if FileManager.default.fileExists(atPath: gameExe) {
            appendOutput("\(game.label) is already installed.\n")
            appendOutput("Launching \(game.exe)...\n\n")
            launchInstalledGame(gameExe: gameExe, gameDir: gameDir)
            return
        }

        appendOutput("Installing \(game.label)...\n")
        appendOutput("Cloning from \(game.repo) (this may take several minutes)...\n\n")

        DispatchQueue.main.async { [weak self] in
            self?.statusLabel.stringValue = "Installing..."
        }

        try? FileManager.default.createDirectory(atPath: gamesDir, withIntermediateDirectories: true)

        let gitProcess = Process()
        gitProcess.executableURL = URL(fileURLWithPath: "/usr/bin/git")
        gitProcess.arguments = ["clone", "--depth", "1", "--single-branch", "-b", game.branch, "--progress", game.repo, gameDir]

        let pipe = Pipe()
        gitProcess.standardOutput = pipe
        gitProcess.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if data.isEmpty { return }
            if let text = String(data: data, encoding: .utf8) {
                self?.appendOutput(text)
            }
        }

        do {
            try gitProcess.run()
            gitProcess.waitUntilExit()
        } catch {
            pipe.fileHandleForReading.readabilityHandler = nil
            appendError("Failed to run git clone: \(error.localizedDescription)\n")
            DispatchQueue.main.async { [weak self] in
                self?.markStopped(status: "Install failed", color: .systemRed)
            }
            return
        }

        pipe.fileHandleForReading.readabilityHandler = nil

        if gitProcess.terminationStatus != 0 {
            appendError("\ngit clone failed (exit \(gitProcess.terminationStatus))\n")
            DispatchQueue.main.async { [weak self] in
                self?.markStopped(status: "Install failed", color: .systemRed)
            }
            return
        }

        guard FileManager.default.fileExists(atPath: gameExe) else {
            appendError("\nInstall completed but \(game.exe) not found.\n")
            DispatchQueue.main.async { [weak self] in
                self?.markStopped(status: "Install incomplete", color: .systemRed)
            }
            return
        }

        appendOutput("\n\(game.label) installed successfully!\n")
        appendOutput("Launching \(game.exe)...\n\n")
        launchInstalledGame(gameExe: gameExe, gameDir: gameDir)
    }

    func launchInstalledGame(gameExe: String, gameDir: String) {
        DispatchQueue.main.async { [weak self] in
            self?.statusLabel.stringValue = "Running game"
        }

        if let rgssVersion = detectRPGMaker(gameDir: gameDir) {
            launchWithMkxpz(gameDir: gameDir, rgssVersion: rgssVersion)
        } else {
            executeWine(exePath: gameExe)
        }
    }

    // MARK: - RPG Maker Detection & Native mkxp-z

    func detectRPGMaker(gameDir: String) -> Int? {
        let gameIni = gameDir + "/Game.ini"
        guard let contents = try? String(contentsOfFile: gameIni, encoding: .utf8) else { return nil }

        for line in contents.components(separatedBy: .newlines) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            guard let eqIdx = trimmed.firstIndex(of: "=") else { continue }
            let key = trimmed[..<eqIdx].trimmingCharacters(in: .whitespaces).lowercased()
            guard key == "library" else { continue }
            let lib = trimmed[trimmed.index(after: eqIdx)...].trimmingCharacters(in: .whitespaces).lowercased()
            if lib.contains("rgss1") { return 1 }
            if lib.contains("rgss2") { return 2 }
            if lib.contains("rgss3") { return 3 }
        }
        return nil
    }

    /// Infinite Fusion downloads Data/Scripts/DownloadedSettings.rb on every start, and the current upstream copy
    /// blanks the sprite download URLs ("") that the game's own Settings define. Drop those blank overrides so the
    /// built-in URLs stay in effect. Only lines whose constant has a non-blank default in 001_Settings.rb are touched.
    func restoreBlankedDownloadURLs(gameDir: String) {
        let scripts = gameDir + "/Data/Scripts"
        let downloaded = scripts + "/DownloadedSettings.rb"
        guard let defaults = try? String(contentsOfFile: scripts + "/001_Settings.rb", encoding: .utf8),
              let current = try? String(contentsOfFile: downloaded, encoding: .utf8),
              let blank = try? NSRegularExpression(pattern: "^\\s*([A-Z0-9_]+_URL)\\s*=\\s*\"\"\\s*$") else { return }
        var removed: [String] = []
        let kept = current.components(separatedBy: "\n").filter { line in
            let range = NSRange(line.startIndex..., in: line)
            guard let m = blank.firstMatch(in: line, range: range),
                  let r = Range(m.range(at: 1), in: line) else { return true }
            let name = String(line[r])
            guard defaults.range(of: "\(name) = \"http") != nil else { return true }
            removed.append(name)
            return false
        }
        guard !removed.isEmpty else { return }
        do {
            try kept.joined(separator: "\n").write(toFile: downloaded, atomically: true, encoding: .utf8)
            appendInfo("Restored \(removed.count) sprite download URL(s) in DownloadedSettings.rb\n")
        } catch {
            appendError("Could not update DownloadedSettings.rb: \(error.localizedDescription)\n")
        }
    }

    func launchWithMkxpz(gameDir: String, rgssVersion: Int) {
        let engineNames = [1: "RPG Maker XP", 2: "RPG Maker VX", 3: "RPG Maker VX Ace"]
        let engineName = engineNames[rgssVersion] ?? "RPG Maker"
        appendInfo("\(engineName) game detected (RGSS\(rgssVersion)).\n")
        appendInfo("Pipeline: Native ARM64 mkxp-z + ANGLE Metal (no Wine needed)\n\n")

        let bundleResources = Bundle.main.bundlePath + "/Contents/Resources"
        let mkxpzBin = bundleResources + "/mkxp-z/mkxp-z.aarch64"

        guard FileManager.default.fileExists(atPath: mkxpzBin) else {
            appendError("mkxp-z binary not found in app bundle.\n")
            DispatchQueue.main.async { [weak self] in
                self?.markStopped(status: "Error", color: .systemRed)
            }
            return
        }

        ensureArialFonts(gameDir: gameDir)
        restoreBlankedDownloadURLs(gameDir: gameDir)

        let mkxpJson = generateMkxpJson(gameDir: gameDir, rgssVersion: rgssVersion)
        let mkxpzDir = bundleResources + "/mkxp-z"
        let configPath = mkxpzDir + "/mkxp.json"
        try? mkxpJson.write(toFile: configPath, atomically: true, encoding: .utf8)

        appendOutput("Launching natively on ARM64...\n\n")

        let process = Process()
        process.executableURL = URL(fileURLWithPath: mkxpzBin)
        process.currentDirectoryURL = URL(fileURLWithPath: mkxpzDir)

        var env = ProcessInfo.processInfo.environment
        env["DYLD_LIBRARY_PATH"] = nil
        env["DYLD_FALLBACK_LIBRARY_PATH"] = nil
        process.environment = env

        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if data.isEmpty { return }
            if let text = String(data: data, encoding: .utf8) {
                self?.appendOutput(text)
            }
        }

        currentProcess = process

        var launchFailed = false
        do {
            try process.run()
            process.waitUntilExit()
        } catch {
            appendError("\nFailed to launch mkxp-z: \(error.localizedDescription)\n")
            launchFailed = true
        }

        pipe.fileHandleForReading.readabilityHandler = nil
        currentProcess = nil

        let exitCode: Int32 = launchFailed ? -1 : process.terminationStatus
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.markStopped(
                status: exitCode == 0 ? "Ready" : (exitCode == 15 ? "Stopped" : "Exit: \(exitCode)"),
                color: exitCode == 0 ? .systemGreen : (exitCode == 15 ? .secondaryLabelColor : .systemRed)
            )
            if exitCode == 0 {
                self.appendOutput("\nFinished (exit 0)\n")
            } else if exitCode == 15 {
                self.appendInfo("\nStopped by user\n")
            } else {
                self.appendError("\nExited with code \(exitCode)\n")
            }
        }
    }

    func generateMkxpJson(gameDir: String, rgssVersion: Int) -> String {
        let escaped = gameDir.replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")

        var screenW = 640
        var screenH = 480
        if let res = detectGameResolution(gameDir: gameDir) {
            screenW = res.0
            screenH = res.1
        }

        let fontSubs = readGameFontSubs(gameDir: gameDir)
        let fontSubJson: String
        if fontSubs.isEmpty {
            fontSubJson = ""
        } else {
            let entries = fontSubs.map { sub -> String in
                let escaped = sub.replacingOccurrences(of: "\\", with: "\\\\")
                    .replacingOccurrences(of: "\"", with: "\\\"")
                return "\"\(escaped)\""
            }.joined(separator: ",\n            ")
            fontSubJson = ",\n        \"fontSub\": [\n            \(entries)\n        ]"
        }

        return """
        {
            "rgssVersion": \(rgssVersion),
            "execName": "Game",
            "gameFolder": "\(escaped)",
            "fullscreen": false,
            "fixedAspectRatio": true,
            "winResizable": true,
            "defScreenW": \(screenW),
            "defScreenH": \(screenH),
            "printFPS": true,
            "anyAltToggleFS": false,
            "enableBlitting": true,
            "maxTextureSize": 0,
            "syncToRefreshrate": false,
            "solidFonts": false,
            "fontHeightReporting": 1\(fontSubJson)
        }
        """
    }

    func detectGameResolution(gameDir: String) -> (Int, Int)? {
        let searchPaths = [
            gameDir + "/Data/Scripts/001_Settings.rb",
            gameDir + "/Scripts/001_Settings.rb"
        ]
        for path in searchPaths {
            guard let contents = try? String(contentsOfFile: path, encoding: .utf8) else { continue }
            var w: Int?
            var h: Int?
            for line in contents.components(separatedBy: .newlines) {
                let trimmed = line.trimmingCharacters(in: .whitespaces)
                if trimmed.contains("SCREEN_WIDTH") && trimmed.contains("=") && !trimmed.hasPrefix("#") {
                    if let num = extractNumber(from: trimmed) { w = num }
                }
                if trimmed.contains("SCREEN_HEIGHT") && trimmed.contains("=") && !trimmed.hasPrefix("#") {
                    if let num = extractNumber(from: trimmed) { h = num }
                }
            }
            if let w = w, let h = h { return (w, h) }
        }
        return nil
    }

    func readGameFontSubs(gameDir: String) -> [String] {
        let configPath = gameDir + "/mkxp.json"
        guard let data = FileManager.default.contents(atPath: configPath),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let subs = json["fontSub"] as? [String] else { return [] }
        return subs
    }

    private func extractNumber(from line: String) -> Int? {
        guard let eqIdx = line.firstIndex(of: "=") else { return nil }
        let afterEq = line[line.index(after: eqIdx)...].trimmingCharacters(in: .whitespaces)
        let numStr = afterEq.prefix(while: { $0.isNumber })
        return Int(numStr)
    }

    func ensureArialFonts(gameDir: String) {
        let fm = FileManager.default
        let fontsDir = gameDir + "/Fonts"
        try? fm.createDirectory(atPath: fontsDir, withIntermediateDirectories: true)

        let systemFonts = "/System/Library/Fonts/Supplemental"
        let needed = ["Arial.ttf", "Arial Bold.ttf"]
        for font in needed {
            let dest = fontsDir + "/" + font
            if fm.fileExists(atPath: dest) { continue }
            let src = systemFonts + "/" + font
            if fm.fileExists(atPath: src) {
                try? fm.copyItem(atPath: src, toPath: dest)
                appendInfo("Installed font: \(font)\n")
            }
        }
    }

    func markStopped(status: String, color: NSColor) {
        isRunning = false
        runButton.isEnabled = true
        stopButton.isEnabled = false
        openButton.isEnabled = true
        statusLabel.stringValue = status
        statusLabel.textColor = color
    }

    func makeWineEnv() -> (bundleContents: String, wineDir: String, env: [String: String]) {
        let bundleContents = Bundle.main.bundlePath + "/Contents"
        let wineDir = bundleContents + "/Resources/wine"
        var env = ProcessInfo.processInfo.environment
        env["WINEPREFIX"] = Self.prefixDir
        env["WINESERVER"] = wineDir + "/bin/wineserver"
        env["WINELOADERNOEXEC"] = "1"
        env["WINEDEBUG"] = "-all"
        env["DYLD_LIBRARY_PATH"] = wineDir + "/lib:" + bundleContents + "/Frameworks"
        env["DYLD_FALLBACK_LIBRARY_PATH"] = bundleContents + "/Frameworks:" + wineDir + "/lib:/usr/lib"
        env["VK_ICD_FILENAMES"] = bundleContents + "/Resources/vulkan/icd.d/MoltenVK_icd.json"
        env["VK_DRIVER_FILES"] = bundleContents + "/Resources/vulkan/icd.d/MoltenVK_icd.json"
        env["MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS"] = "1"
        env["WINEDLLOVERRIDES"] = "d3d11,dxgi=b;winemetal=b"
        env["PATH"] = wineDir + "/bin:" + (env["PATH"] ?? "")
        return (bundleContents, wineDir, env)
    }

    func initWinePrefix(wineDir: String, env: [String: String]) -> Bool {
        let prefixPath = Self.prefixDir
        let systemReg = prefixPath + "/system.reg"
        let dosdevices = prefixPath + "/dosdevices/z:"

        if FileManager.default.fileExists(atPath: systemReg) && FileManager.default.fileExists(atPath: dosdevices) {
            createPrefixDirectories(prefixPath: prefixPath)
            populateSystem32(prefixPath: prefixPath, env: env)
            return true
        }

        appendInfo("Initializing Wine prefix (first run, may take a moment)...\n")

        let boot = Process()
        boot.executableURL = URL(fileURLWithPath: wineDir + "/bin/wine")
        boot.arguments = ["wineboot", "--init"]
        var bootEnv = env
        bootEnv["WINEDEBUG"] = "-all"
        boot.environment = bootEnv

        let pipe = Pipe()
        boot.standardOutput = pipe
        boot.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if data.isEmpty { return }
            if let text = String(data: data, encoding: .utf8) {
                self?.appendOutput(text)
            }
        }

        do {
            try boot.run()
            boot.waitUntilExit()
        } catch {
            pipe.fileHandleForReading.readabilityHandler = nil
            appendError("Failed to run wineboot: \(error.localizedDescription)\n")
            return false
        }

        pipe.fileHandleForReading.readabilityHandler = nil

        // Wait for wineserver to flush registry to disk before checking
        if let serverPath = env["WINESERVER"] {
            let wait = Process()
            wait.executableURL = URL(fileURLWithPath: serverPath)
            wait.arguments = ["-w"]
            wait.environment = env
            try? wait.run()
            wait.waitUntilExit()
        }

        let prefixOK = FileManager.default.fileExists(atPath: prefixPath + "/system.reg")
        if boot.terminationStatus != 0 && !prefixOK {
            appendError("wineboot failed (exit \(boot.terminationStatus))\n")
            return false
        }

        createPrefixDirectories(prefixPath: prefixPath)
        populateSystem32(prefixPath: prefixPath, env: env)

        // Fix arch tag — wineboot under x86_64 JIT may write win32
        for reg in ["system.reg", "user.reg", "userdef.reg"] {
            let path = prefixPath + "/" + reg
            if let content = try? String(contentsOfFile: path, encoding: .utf8) {
                let fixed = content.replacingOccurrences(of: "#arch=win32", with: "#arch=win64")
                if fixed != content {
                    try? fixed.write(toFile: path, atomically: true, encoding: .utf8)
                }
            }
        }

        appendInfo("Wine prefix ready.\n\n")
        return true
    }

    func createPrefixDirectories(prefixPath: String) {
        let fm = FileManager.default
        let dirs = [
            prefixPath + "/drive_c/windows/system32",
            prefixPath + "/drive_c/windows/syswow64",
            prefixPath + "/drive_c/windows/temp",
            prefixPath + "/drive_c/users/" + NSUserName() + "/AppData/Local",
            prefixPath + "/drive_c/users/" + NSUserName() + "/AppData/Roaming",
            prefixPath + "/drive_c/users/" + NSUserName() + "/AppData/LocalLow",
            prefixPath + "/drive_c/users/" + NSUserName() + "/Documents",
            prefixPath + "/drive_c/users/" + NSUserName() + "/Desktop",
            prefixPath + "/drive_c/users/" + NSUserName() + "/Downloads",
            prefixPath + "/drive_c/users/Public/Temp",
            prefixPath + "/drive_c/ProgramData",
            prefixPath + "/drive_c/Program Files",
            prefixPath + "/drive_c/Program Files (x86)",
            prefixPath + "/dosdevices",
        ]
        for dir in dirs {
            try? fm.createDirectory(atPath: dir, withIntermediateDirectories: true)
        }
        let cLink = prefixPath + "/dosdevices/c:"
        let zLink = prefixPath + "/dosdevices/z:"
        if !fm.fileExists(atPath: cLink) {
            try? fm.createSymbolicLink(atPath: cLink, withDestinationPath: "../drive_c")
        }
        if !fm.fileExists(atPath: zLink) {
            try? fm.createSymbolicLink(atPath: zLink, withDestinationPath: "/")
        }
    }

    func populateSystem32(prefixPath: String, env: [String: String]) {
        let fm = FileManager.default
        let bundleContents = Bundle.main.bundlePath + "/Contents"
        let wineDir = bundleContents + "/Resources/wine"
        let wineLib = wineDir + "/lib/wine"

        let sys32 = prefixPath + "/drive_c/windows/system32"
        let syswow64 = prefixPath + "/drive_c/windows/syswow64"
        try? fm.createDirectory(atPath: sys32, withIntermediateDirectories: true)
        try? fm.createDirectory(atPath: syswow64, withIntermediateDirectories: true)

        let extensions: Set<String> = ["dll", "exe", "drv", "sys", "ocx", "cpl", "acm", "ax"]

        func linkPEs(from archDir: String, to destDir: String) -> Int {
            guard let files = try? fm.contentsOfDirectory(atPath: archDir) else { return 0 }
            var count = 0
            for file in files {
                let ext = (file as NSString).pathExtension.lowercased()
                guard extensions.contains(ext) else { continue }
                let dest = destDir + "/" + file
                if fm.fileExists(atPath: dest) { continue }
                let src = archDir + "/" + file
                try? fm.createSymbolicLink(atPath: dest, withDestinationPath: src)
                count += 1
            }
            return count
        }

        var total = 0
        total += linkPEs(from: wineLib + "/aarch64-windows", to: sys32)
        total += linkPEs(from: wineLib + "/arm64ec-windows", to: sys32)
        total += linkPEs(from: wineLib + "/x86_64-windows", to: sys32)
        total += linkPEs(from: wineLib + "/i386-windows", to: syswow64)
        if total > 0 {
            appendInfo("Populated system32/syswow64 with \(total) Wine DLLs.\n")
        }

        let fontsDir = prefixPath + "/drive_c/windows/Fonts"
        let fontsSrc = wineDir + "/share/wine/fonts"
        try? fm.createDirectory(atPath: fontsDir, withIntermediateDirectories: true)
        if let fontFiles = try? fm.contentsOfDirectory(atPath: fontsSrc) {
            var fc = 0
            for file in fontFiles {
                let dest = fontsDir + "/" + file
                if fm.fileExists(atPath: dest) { continue }
                try? fm.createSymbolicLink(atPath: dest, withDestinationPath: fontsSrc + "/" + file)
                fc += 1
            }
            if fc > 0 { appendInfo("Installed \(fc) Wine fonts.\n") }
        }

        let tempDir = prefixPath + "/drive_c/users/" + NSUserName() + "/AppData/Local/Temp"
        try? fm.createDirectory(atPath: tempDir, withIntermediateDirectories: true)

        configureGitSSL(prefixPath: prefixPath)
    }

    func configureGitSSL(prefixPath: String) {
        let fm = FileManager.default
        let userHome = prefixPath + "/drive_c/users/" + NSUserName()
        let gitConfigPath = userHome + "/.gitconfig"
        if !fm.fileExists(atPath: gitConfigPath) {
            let config = "[http]\n\tsslBackend = openssl\n"
            try? config.write(toFile: gitConfigPath, atomically: true, encoding: .utf8)
        }

        let windowsDir = prefixPath + "/drive_c/windows"
        guard let entries = try? fm.contentsOfDirectory(atPath: windowsDir) else { return }
        for entry in entries where entry.hasPrefix("_MEI") {
            let gitEtc = windowsDir + "/" + entry + "/vendor/git/etc/gitconfig"
            let caCert = windowsDir + "/" + entry + "/vendor/git/mingw64/etc/ssl/certs/ca-bundle.crt"
            guard fm.fileExists(atPath: gitEtc), fm.fileExists(atPath: caCert) else { continue }
            let existing = (try? String(contentsOfFile: gitEtc, encoding: .utf8)) ?? ""
            if existing.contains("sslBackend") { continue }
            let winCert = "C:/windows/" + entry + "/vendor/git/mingw64/etc/ssl/certs/ca-bundle.crt"
            let patch = "\n[http]\n\tsslBackend = openssl\n\tsslCAInfo = \(winCert)\n"
            try? (existing + patch).write(toFile: gitEtc, atomically: true, encoding: .utf8)
        }
    }

    /// True for a 64-bit PE image (optional header magic 0x20b, PE32+).
    func isPE64(path: String) -> Bool {
        guard let fh = FileHandle(forReadingAtPath: path) else { return false }
        defer { try? fh.close() }
        guard let dos = try? fh.read(upToCount: 0x40), dos.count == 0x40,
              dos[0] == 0x4D, dos[1] == 0x5A else { return false }
        let peOffset = UInt64(dos[0x3C]) | UInt64(dos[0x3D]) << 8 | UInt64(dos[0x3E]) << 16 | UInt64(dos[0x3F]) << 24
        guard (try? fh.seek(toOffset: peOffset)) != nil,
              let hdr = try? fh.read(upToCount: 26), hdr.count == 26,
              hdr[0] == 0x50, hdr[1] == 0x45, hdr[2] == 0, hdr[3] == 0 else { return false }
        return UInt16(hdr[24]) | UInt16(hdr[25]) << 8 == 0x20B
    }

    /// Installs the bundled 64-bit emulator (FEX ARM64EC) into the prefix and registers it with Wine. Only done for
    /// 64-bit programs, so 32-bit use is untouched. Returns false if the bundle has no emulator or setup failed.
    func ensure64BitEmulator(wineDir: String, env: [String: String]) -> Bool {
        let bundled = Bundle.main.bundlePath + "/Contents/Resources/fex64/libarm64ecfex.dll"
        let fm = FileManager.default
        guard fm.fileExists(atPath: bundled) else { return false }
        let installed = Self.prefixDir + "/drive_c/windows/system32/libarm64ecfex.dll"
        let marker = Self.prefixDir + "/.fex64-registered"
        let bundledSize = (try? fm.attributesOfItem(atPath: bundled)[.size] as? UInt64) ?? 0
        let bundledDate = (try? fm.attributesOfItem(atPath: bundled)[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
        let stamp = "\(bundledSize):\(Int(bundledDate))"
        if fm.fileExists(atPath: installed), (try? String(contentsOfFile: marker, encoding: .utf8)) == stamp { return true }

        appendInfo("Setting up the 64-bit emulator (first 64-bit run)...\n")
        try? fm.removeItem(atPath: installed)
        do { try fm.copyItem(atPath: bundled, toPath: installed) } catch {
            appendError("Could not install the 64-bit emulator: \(error.localizedDescription)\n")
            return false
        }
        let reg = Process()
        reg.executableURL = URL(fileURLWithPath: wineDir + "/bin/wine")
        reg.arguments = ["reg", "add", "HKLM\\Software\\Microsoft\\Wow64\\amd64", "/ve", "/d", "libarm64ecfex.dll", "/f"]
        var regEnv = env
        regEnv["WINEDEBUG"] = "-all"
        reg.environment = regEnv
        reg.standardOutput = FileHandle.nullDevice
        reg.standardError = FileHandle.nullDevice
        do { try reg.run(); reg.waitUntilExit() } catch { return false }
        guard reg.terminationStatus == 0 else { return false }
        try? stamp.write(toFile: marker, atomically: true, encoding: .utf8)
        return true
    }

    func executeWine(exePath: String) {
        let is64 = isPE64(path: exePath)
        if is64 {
            let (_, wineDir, baseEnv) = makeWineEnv()
            let ready = initWinePrefix(wineDir: wineDir, env: baseEnv) && ensure64BitEmulator(wineDir: wineDir, env: baseEnv)
            if !ready {
                appendError("\n64-bit programs aren't supported in this build (emulator missing).\n")
                DispatchQueue.main.async { [weak self] in
                    self?.markStopped(status: "Unsupported (64-bit)", color: .systemRed)
                }
                return
            }
            appendInfo("64-bit program: running with the experimental 64-bit emulator (early, expect bugs).\n")
        }
        appendInfo("Pipeline: PE \u{2192} Wine WoW64 \u{2192} FEXCore JIT \u{2192} ARM64 + DXMT Metal\n\n")
        let (_, wineDir, baseEnv) = makeWineEnv()

        if !initWinePrefix(wineDir: wineDir, env: baseEnv) {
            DispatchQueue.main.async { [weak self] in
                self?.markStopped(status: "Error", color: .systemRed)
            }
            return
        }

        let env = baseEnv
        let wineBin = wineDir + "/bin/wine"

        // fex-run (bundled) applies the per-program profile (flags, DLL overrides, restart on startup freeze)
        // and the FEX settings Chromium-based programs need; fall back to plain wine if it is missing.
        let runner = wineDir + "/../bin/fex-run"
        let useRunner = FileManager.default.isExecutableFile(atPath: runner)

        let process = Process()
        process.executableURL = URL(fileURLWithPath: useRunner ? runner : wineBin)
        process.arguments = [exePath]
        process.currentDirectoryURL = URL(fileURLWithPath: exePath).deletingLastPathComponent()
        process.environment = env

        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if data.isEmpty { return }
            if let text = String(data: data, encoding: .utf8) {
                self?.appendOutput(text)
            }
        }

        currentProcess = process

        var launchFailed = false
        do {
            try process.run()
            process.waitUntilExit()
        } catch {
            appendError("\nFailed to launch: \(error.localizedDescription)\n")
            launchFailed = true
        }

        pipe.fileHandleForReading.readabilityHandler = nil
        currentProcess = nil

        let exitCode: Int32 = launchFailed ? -1 : process.terminationStatus
        if exitCode == 0 {
            appendOutput("\nFinished (exit 0)\n")
        } else if exitCode == 15 {
            appendInfo("\nStopped by user\n")
        } else {
            appendError("\nExited with code \(exitCode)\n")
        }
        DispatchQueue.main.async { [weak self] in
            self?.markStopped(
                status: exitCode == 0 ? "Ready" : (exitCode == 15 ? "Stopped" : "Exit: \(exitCode)"),
                color: exitCode == 0 ? .systemGreen : (exitCode == 15 ? .secondaryLabelColor : .systemRed)
            )
        }
    }

    func killWineserver() {
        let bundleContents = Bundle.main.bundlePath + "/Contents"
        let wineDir = bundleContents + "/Resources/wine"
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: wineDir + "/bin/wineserver")
        proc.arguments = ["-k"]
        proc.environment = [
            "WINEPREFIX": Self.prefixDir
        ]
        try? proc.run()
    }

    // MARK: - App lifecycle

    var pendingOpenFile: String?

    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        guard filename.lowercased().hasSuffix(".exe") else { return false }
        if window != nil {
            pathField.stringValue = filename
            runExe(path: filename)
        } else {
            pendingOpenFile = filename
        }
        return true
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }

    func applicationWillTerminate(_ notification: Notification) {
        currentProcess?.terminate()
        // let fex-run close the program politely (up to ~45 s) before the Wine server is stopped
        let deadline = Date().addingTimeInterval(45)
        while let p = currentProcess, p.isRunning, Date() < deadline { Thread.sleep(forTimeInterval: 0.25) }
        let wineDir = Bundle.main.bundlePath + "/Contents/Resources/wine"
        if FileManager.default.fileExists(atPath: wineDir + "/bin/wineserver") {
            killWineserver()
        }
    }
}

// MARK: - Drop target

class DropView: NSView {
    weak var appDelegate: AppDelegate?
    var isDragOver = false

    init(frame: NSRect, delegate: AppDelegate) {
        self.appDelegate = delegate
        super.init(frame: frame)
        registerForDraggedTypes([.fileURL])
    }

    required init?(coder: NSCoder) { fatalError() }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        if hasExeFile(sender) {
            isDragOver = true
            needsDisplay = true
            return .copy
        }
        return []
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        isDragOver = false
        needsDisplay = true
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        isDragOver = false
        needsDisplay = true
        guard let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self]) as? [URL] else {
            return false
        }
        for url in urls where url.pathExtension.lowercased() == "exe" {
            appDelegate?.pathField.stringValue = url.path
            appDelegate?.runExe(path: url.path)
            return true
        }
        return false
    }

    override func draw(_ dirtyRect: NSRect) {
        if isDragOver {
            NSColor.controlAccentColor.withAlphaComponent(0.15).setFill()
            bounds.fill()
            let border = NSBezierPath(roundedRect: bounds.insetBy(dx: 4, dy: 4), xRadius: 8, yRadius: 8)
            border.lineWidth = 2
            NSColor.controlAccentColor.withAlphaComponent(0.5).setStroke()
            border.stroke()
        }
    }

    private func hasExeFile(_ info: NSDraggingInfo) -> Bool {
        guard let urls = info.draggingPasteboard.readObjects(forClasses: [NSURL.self]) as? [URL] else {
            return false
        }
        return urls.contains { $0.pathExtension.lowercased() == "exe" }
    }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
