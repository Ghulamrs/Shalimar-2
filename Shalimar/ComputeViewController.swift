//
//  ComputeViewController.swift
//  Shalimar: G. R. Akhtar
//
//  Created by Home on 5/21/19.
//  Copyright © 2019-2026 G. R. Akhtar. All rights reserved.
//

import UIKit
import Messages
import MessageUI
import VisionKit
import Vision

class ComputeViewController: UIViewController, Storyboarded, UITextViewDelegate, VNDocumentCameraViewControllerDelegate {
    weak var coordinator: MainCoordinator?
    @IBOutlet var lineview: UITextView!
    @IBOutlet var program: UITextView!

    // Index of a space the keyboard's "." shortcut may be about to overwrite, set by
    // shouldChangeTextIn and read once by the change that follows it.
    private var periodWatch: Int?
    @IBOutlet var console: UITextView!
    var count: Int = 0
    var source: String {
        """
        fun <> = main() {
           ? "Hello world!"
        }
        """
    }

    override func viewDidLoad() {
        super.viewDidLoad()

        // The editor is a TextKit 1 text view, and it has to be one from the start.
        //
        // A UITextView built by a storyboard begins on TextKit 2, and reaching for
        // textStorage or layoutManager - which the syntax colouring, the line-number
        // gutter and the error strips all do - converts it down to TextKit 1 on the spot.
        // Converting is not the same as starting there: the selection machinery is set up
        // before the switch and does not follow it, so a selection kept its grab handles
        // but lost the highlight band that says how far it reaches. Copy and paste both
        // worked, which is what made it look like a drawing bug rather than a text engine
        // one. Asking for the layout manager here, before the view is laid out and before
        // any selection exists, settles the engine first and everything else follows it.
        _ = program.layoutManager

        // SF Mono, which has no name to pass to UIFont(name:) on iOS - the system API is
        // the only way to reach it. Set before lineview copies it below so the gutter and
        // the editor keep the same line height and the numbers stay on their lines.
        program.font = UIFont.monospacedSystemFont(ofSize: 14, weight: .medium)

        // Grey, a shade darker than the 0.75 the gutter carried against its old white
        // strip: the numbers are reference, not code, so they should sit behind the
        // violet without disappearing - 0.60 reads about 2.9:1 on white, where 0.75 was
        // 1.8:1 and hard to count down in daylight.
        lineview.textColor = UIColor.init(white: 0.6, alpha: 1)
        lineview.font = program.font
        lineview.isScrollEnabled = true
        // The gutter is a mirror, never a control. Left touchable it was its own scroll
        // view: a stray finger could drag the numbers out of line with the code, and the
        // offset it was handed back each frame fought that drag instead of following it.
        // Its own bounce stays on so it can be pushed past its bounds by the editor's,
        // which is what keeps a number beside its line through the whole spring.
        lineview.isUserInteractionEnabled = false
        // The gutter is 40pt wide in the storyboard and its numbers are right-aligned, so
        // its own padding is width the digits cannot use. Zeroing it buys back 8pt, about
        // what a fourth digit costs - which the old fixed 1...99 gutter never needed and
        // a growing one does. Four digits measure 34.6pt at 14pt SF Mono, so 38pt of usable
        // width leaves the column room to reach 9999 without a number wrapping - and a
        // wrapped number would push every line below it out of register, not just its own.
        // The vertical inset has to keep matching the editor's, or every number sits off
        // the line it counts.
        lineview.textContainerInset = UIEdgeInsets(top: program.textContainerInset.top, left: 0,
                                                   bottom: program.textContainerInset.bottom, right: 2)
        lineview.textContainer.lineFragmentPadding = 0

        program.delegate = self
        // Shalimar source is ASCII and its identifiers are case-sensitive, so iOS's text
        // "helpers" all corrupt a program invisibly: smart quotes replace the " the lexer
        // scans for, smart dashes fuse "--" into an en-dash, and sentence capitalization
        // renames a variable. Set every trait here rather than in the storyboard so this
        // stays the single place the editor's input behaviour is defined.
        // .asciiCapable is only a hint - the globe key and pasting both bypass it, so it
        // is a convenience, not a guarantee that the text stays ASCII.
        program.autocapitalizationType = .none
        program.autocorrectionType = .no
        program.spellCheckingType = .no
        program.smartQuotesType = .no
        program.smartDashesType = .no
        program.smartInsertDeleteType = .no
        program.keyboardType = .asciiCapable
        // The mint is back, lightened from 0.8 red to 0.9: enough tint to separate the page
        // from the white column beside it, pale enough that the violet still carries -
        // 4.1:1 here, against 3.9:1 on the original mint and 4.3:1 on plain white.
        program.backgroundColor = UIColor.init(displayP3Red: 0.9, green: 1, blue: 0.975, alpha: 1)
        console.backgroundColor = UIColor.init(white: 0.8, alpha: 0.5)
        // The original violet, deepened to carry at 14pt but not to black - about 4.1:1
        // against the mint background set above. Not .label or a dynamic colour: that
        // background is fixed light, so anything inverting in dark mode would land white
        // on mint.
        program.textColor = Self.codeColour

        // Everything used to be red, which meant red carried no signal - a program's own
        // output shouted and its errors blended into it. Output is now near-black and the
        // colour is spent only where something went wrong.
        console.font = UIFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        console.textColor = ConsoleStyle.output.color
        console.isScrollEnabled = true
        console.isEditable = false

        // Before the console handle, which anchors itself to the foot of the editor and
        // needs the pane to exist to anchor to.
        installEditorPane()
        installConsoleHandle()

        // The keyboard covers the bottom of the screen, and the editor is what is under it.
        // willChangeFrame rather than willShow: it is the one that also fires for a hardware
        // keyboard connecting, a floating keyboard being dragged, and the height changing
        // under a predictive bar - all of which move the floor the text has to clear.
        NotificationCenter.default.addObserver(self, selector: #selector(keyboardChanged),
                                               name: UIResponder.keyboardWillChangeFrameNotification,
                                               object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(keyboardHidden),
                                               name: UIResponder.keyboardWillHideNotification,
                                               object: nil)

        // The storyboard leaves 20pt between the console and the safe area, which was
        // nothing but white. A line fits there exactly, and it is the one place in the
        // editor that can say what the app is without taking room from the program or the
        // output. Grey, at the console banner's weight: it is the room's label, not
        // something to be read twice.
        //
        // Constrained to the gap rather than given a height, so it stays put if the
        // storyboard's 20pt ever changes, and pinned to the safe area so it clears the
        // home indicator.
        let tagline = UILabel()
        // The years in the same short form as the run banner and the reference footer, so
        // the three places the app names itself all say the same thing.
        tagline.text = "©2019-26 Shalimar 3.0, A mini language compiler."
        tagline.font = UIFont.systemFont(ofSize: 11, weight: .medium)
        // Embossed: the letter is the pale face catching the light and the shadow falls
        // below it, darker, so the line reads as standing off the page. Reversing those
        // two is the whole difference - a shadow lighter than the letter presses it in
        // instead, which is what this looked like before.
        //
        // The editor's own violet, so the footer belongs to the language rather than
        // standing as a fourth signal beside output, warning and error. Red was the
        // obvious choice and the wrong one: this app spends red on things that went
        // wrong, and a red line sitting on screen at all times is red that never means a
        // problem - which is how a reader learns to stop seeing it.
        //
        // The shadow is a deeper shade of the same violet, not a grey: a raised thing
        // casts its shadow in its own colour, and a grey edge under a violet face reads
        // as two materials rather than one letter with a lit top.
        tagline.textColor = Self.codeColour
        tagline.shadowColor = UIColor(displayP3Red: 0.32, green: 0.08, blue: 0.42, alpha: 1)
        tagline.shadowOffset = CGSize(width: 0, height: 1)
        tagline.textAlignment = .center
        tagline.adjustsFontSizeToFitWidth = true
        tagline.minimumScaleFactor = 0.8
        tagline.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(tagline)
        NSLayoutConstraint.activate([
            tagline.topAnchor.constraint(equalTo: console.bottomAnchor),
            tagline.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor),
            tagline.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 16),
            tagline.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -16)
        ])

        // A new program is typed in rather than pasted in, once the view is on screen -
        // see viewDidAppear. An opened file is not: watching a file you already wrote
        // being retyped would be a wait, not a welcome.
        if !coordinator!.fileURL.isEmpty {
            load(fileName: coordinator!.fileURL)
        }

        let tap2 = UITapGestureRecognizer(target: self, action: #selector(doubleTapped))
        tap2.numberOfTapsRequired = 2
        console.addGestureRecognizer(tap2)

        let tap1 = UITapGestureRecognizer(target: self, action: #selector(textTapped(_:)))
        tap1.numberOfTapsRequired = 1
        // Without this the first tap of a double tap fires the single-tap handler too, so
        // a line went into the editor before doubleTapped could see whether the editor was
        // empty - and it never was, because the tap that arrived first had just filled it.
        tap1.require(toFail: tap2)
        console.addGestureRecognizer(tap1)

        // Both on the right (not left) so the automatic back button stays visible.
        //
        // A colour each, at the top of the P3 gamut: these are the pure primaries, not
        // sRGB's, so they are as saturated as the screen can render. The pill behind them
        // is a pale pink - sampled at (1.0, 0.78, 1.0) - which is why "brighter" here means
        // more chroma and not more light: the pale blue these started as measured 1.4:1
        // against it and the pure one below holds 4.6:1.
        //
        // Green is the exception and cannot be pushed with the other two. Its pure form is
        // the lightest primary there is, so on a pale ground it goes the wrong way - full
        // P3 green measures 1.04:1 on this pill, a glyph you cannot find. This is the most
        // saturated green that still carries; a brighter one needs a darker pill.
        // The colours are already fully opaque, so what makes them read stronger is ink,
        // not alpha: a heavier stroke, and the filled variant of the two symbols that have
        // one. A filled glyph is several times the coloured area of its outline, which is
        // the only lever left for the green - it cannot be brightened, but it can cover
        // more of the pill.
        let heavy = UIImage.SymbolConfiguration(pointSize: 19, weight: .bold)

        let scan = UIBarButtonItem(image: UIImage(systemName: "doc.text.viewfinder", withConfiguration: heavy),
            style: .plain, target: self, action: #selector(scanTapped))
        scan.tintColor = UIColor(displayP3Red: 1.0, green: 1.0, blue: 0.0, alpha: 1)

        let save = UIBarButtonItem(image: UIImage(systemName: "square.and.arrow.down.fill", withConfiguration: heavy),
            style: .plain, target: self, action: #selector(saveTapped))
        save.tintColor = UIColor(displayP3Red: 0.0, green: 0.70, blue: 0.10, alpha: 1)

        let help = UIBarButtonItem(image: UIImage(systemName: "questionmark.circle.fill", withConfiguration: heavy),
            style: .plain, target: self, action: #selector(helpTapped))
        help.tintColor = UIColor(displayP3Red: 0.0, green: 0.25, blue: 1.0, alpha: 1)

        navigationItem.rightBarButtonItems = [scan, save, help]

        // Lives in the nav bar (as titleView, dead center) instead of docked below the
        // editor, so the on-screen keyboard - which covers the bottom of the screen -
        // can never overlap it.
        let arrowConfig = UIImage.SymbolConfiguration(pointSize: 26, weight: .bold)
        let runButton = UIButton(type: .system)
        runButton.frame = CGRect(x: 0, y: 0, width: 36, height: 36)
        runButton.setImage(UIImage(systemName: "arrowshape.right.fill", withConfiguration: arrowConfig), for: .normal)
        runButton.tintColor = UIColor.init(displayP3Red: 0.0, green: 0.7, blue: 0.25, alpha: 1)
        runButton.imageView?.contentMode = .scaleAspectFit
        runButton.addTarget(self, action: #selector(ComputeTapped(_:)), for: .touchUpInside)
        navigationItem.titleView = runButton
    }

    // Where a line wraps depends on the page's width, so a rotation or a split-view
    // resize changes the row count without changing the text. The guard inside leaves
    // this cheap when nothing moved.
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        // First: the two below both read the layout, and the layout is not settled until
        // the page knows how wide it is.
        updateCanvasWidth()
        refreshLineNumbers()
        // Same reason as the numbers: a resize rewraps the text, and a strip cut to the
        // old wrapping would sit over the wrong rows.
        layoutErrorStrips()
    }

    // MARK: - The page and the window onto it

    // These used to be one view, and could not be.
    //
    // A UITextView will not scroll sideways. It draws its glyphs into a private subview
    // that it keeps at its own bounds, so text laid out past that edge is not merely
    // clipped by the window - it is never drawn at all, and scrolling right revealed blank
    // page with the end of the line nowhere on it. Widening the text container stops the
    // wrapping and assigning contentSize buys the travel, but neither reaches the view that
    // does the drawing.
    //
    // So the text view stops being the window. It becomes a page of a fixed width that
    // scrolls nothing itself - which is what makes it size to its text, drawing subview and
    // all - and this scroll view is the window moved across it.
    private let editorPane = UIScrollView()

    // The page is 80 columns wide whatever the device is.
    //
    // A width taken from the screen makes the same program a different shape on every phone
    // and in every split-view size, which is the thing that made a wrapped line untrustworthy
    // to read. A column count is the measure programs are actually written to, so the wrap
    // point becomes a property of the page rather than of the hardware. Anything past column
    // 80 still wraps rather than running on: the alternative is text that exists but cannot
    // be reached, which is the failure this whole change exists to remove.
    private static let canvasColumns = 80
    private var canvasWidth: NSLayoutConstraint?

    private func installEditorPane() {
        // Everything the storyboard said about the editor describes the window onto it now,
        // so those constraints come off the text view and go back on the pane. Collected
        // before deactivating: taking them out mutates the array being walked.
        let inherited = view.constraints.filter { $0.firstItem === program || $0.secondItem === program }
        NSLayoutConstraint.deactivate(inherited)

        editorPane.translatesAutoresizingMaskIntoConstraints = false
        // The mint belongs to the page, but the pane shows through wherever the page is
        // shorter than the window, so it carries the same colour rather than a white gap.
        editorPane.backgroundColor = program.backgroundColor
        editorPane.delegate = self
        // A short program has nothing to scroll - no bar, no travel - and a view that cannot
        // move under the finger reads as a dead one. The bounce is the answer the page gives
        // back: alwaysBounceVertical keeps it for text shorter than the frame, which is the
        // case where UIScrollView would otherwise ignore the drag entirely.
        editorPane.bounces = true
        editorPane.alwaysBounceVertical = true
        // Sideways there is no such answer to give: a program narrower than the window would
        // bounce against travel it does not have.
        editorPane.alwaysBounceHorizontal = false
        // A drag commits to one axis. Without the lock, following a long line to the right
        // drifts down at the same time, and the line arrived at is not the line set out on.
        editorPane.isDirectionalLockEnabled = true
        editorPane.showsHorizontalScrollIndicator = true
        view.addSubview(editorPane)

        program.translatesAutoresizingMaskIntoConstraints = false
        // The line that does the work: a text view that scrolls nothing sizes itself to its
        // text, and the private drawing subview goes with it. Every column of the page is
        // then actually painted, which is what gives the pane something real to move over.
        program.isScrollEnabled = false
        editorPane.addSubview(program)

        let content = editorPane.contentLayoutGuide
        let window = editorPane.frameLayoutGuide
        let width = program.widthAnchor.constraint(equalToConstant: 0)
        canvasWidth = width

        NSLayoutConstraint.activate([
            // The pane, standing exactly where the text view used to stand.
            editorPane.leadingAnchor.constraint(equalTo: lineview.trailingAnchor, constant: 8),
            editorPane.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 8),
            view.safeAreaLayoutGuide.trailingAnchor.constraint(equalTo: editorPane.trailingAnchor,
                                                               constant: 16),
            // The gutter measured itself against the text view and measures itself against
            // the window now - which is the height it always meant. The numbers are a column
            // beside the visible page, not beside the whole program.
            lineview.heightAnchor.constraint(equalTo: editorPane.heightAnchor),

            // The page inside it. Pinned to the content guide on all four sides, so the page
            // is what the pane scrolls over.
            program.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            program.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            program.topAnchor.constraint(equalTo: content.topAnchor),
            program.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            width,
            // A short program still fills the window, so the mint reaches the foot of the
            // pane instead of stopping under the last line.
            program.heightAnchor.constraint(greaterThanOrEqualTo: window.heightAnchor)
        ])

        // 'consoleShare' was the editor's two-thirds of the screen, said about the text view.
        // The console pane toggles it open and shut, so the replacement has to be the one
        // this controller holds rather than the storyboard's, which went inactive above.
        consoleShare = editorPane.heightAnchor.constraint(equalTo: console.heightAnchor,
                                                          multiplier: 2)
        consoleShare?.isActive = true
    }

    // 80 columns, or the window's width where that is wider - on an iPad a page narrower
    // than the window would leave the mint stopping short of the right edge with white
    // beyond it. Monospaced, and the colouring pass established that the font measures
    // identical from medium to bold, so one advance times the column count is the whole
    // calculation and no layout pass is needed to find it.
    private func updateCanvasWidth() {
        guard let width = canvasWidth else { return }
        let font = program.font ?? UIFont.monospacedSystemFont(ofSize: 14, weight: .medium)
        let advance = ("0" as NSString).size(withAttributes: [.font: font]).width
        let padding = program.textContainer.lineFragmentPadding * 2
        let inset = program.textContainerInset.left + program.textContainerInset.right
        let page = CGFloat(Self.canvasColumns) * advance + padding + inset

        let wanted = max(page, editorPane.bounds.width)
        // Assigning re-enters layout, and this is called from viewDidLayoutSubviews. The
        // guard is what stops that being a loop.
        guard abs(width.constant - wanted) > 0.5 else { return }
        width.constant = wanted
        // A constant assigned here lands on the next layout pass, but the gutter and the
        // error strips both read the layout in this one - so they would number and cover
        // the old wrapping, one pass behind the page they describe. That is what put a
        // blank continuation row under a line that no longer wraps. Settling the pane now
        // means everything below reads the width just set.
        editorPane.layoutIfNeeded()
    }

    // MARK: - Keyboard

    // The keyboard covers the foot of the screen and the editor is what is under it.
    //
    // The page is not shrunk to fit above it: the page width is the wrap column, and moving
    // it would re-wrap every line the moment the keyboard appeared. Instead the pane is
    // given bottom inset the height of the overlap. The text can then be scrolled up clear
    // of the keyboard, and what ends up behind the keys is empty space past the end of the
    // program rather than the line being typed.
    @objc private func keyboardChanged(_ note: Notification) {
        guard let end = note.userInfo?[UIResponder.keyboardFrameEndUserInfoKey] as? CGRect else { return }
        // Converted from screen coordinates, which is what the notification carries and what
        // a split view or a floating keyboard makes different from the view's own.
        let keyboard = view.convert(end, from: nil)
        let overlap = max(0, editorPane.frame.maxY - keyboard.minY)
        editorPane.contentInset.bottom = overlap
        editorPane.verticalScrollIndicatorInsets.bottom = overlap
        scrollCaretIntoView()
    }

    @objc private func keyboardHidden(_ note: Notification) {
        editorPane.contentInset.bottom = 0
        editorPane.verticalScrollIndicatorInsets.bottom = 0
    }

    // A text view that scrolls itself keeps its own caret in sight for free. This one does
    // not scroll, so the pane has to be told - otherwise typing at the foot of a program
    // walks the caret down behind the keyboard and the line being written cannot be seen.
    private func scrollCaretIntoView() {
        guard program.isFirstResponder, let range = program.selectedTextRange else { return }
        let caret = program.caretRect(for: range.end)
        guard caret.height > 0, !caret.isInfinite, !caret.isNull else { return }
        // Widened before it is shown: a caret scrolled to flush against the edge sits with
        // the character it is about to write already off it.
        editorPane.scrollRectToVisible(program.convert(caret, to: editorPane)
                                        .insetBy(dx: -12, dy: -8), animated: true)
    }

    // Not viewDidLoad: the typing has to be watched to be worth doing, and at load the
    // view is not on screen yet - the whole animation would be over before the push
    // finished. The flag is because this runs again on every return from Help or the
    // scanner, and a program the user has started writing must not be typed over.
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        guard !hasOpened else { return }
        hasOpened = true
        if coordinator!.fileURL.isEmpty { typeIn(Indent.reindented(source)) }
    }

    private var hasOpened = false
    private var typingTimer: Timer?

    // The starting program arrives a character at a time, so a first-time user sees where
    // a program comes from - typed, by them, in a moment - rather than finding one already
    // sitting there. About 40ms a character: quick enough not to be a wait, slow enough
    // that the three lines land one after another.
    //
    // The editor is not editable while this runs. A caret in text that is still arriving
    // would be somewhere else a moment later, and a keystroke would land in the middle of
    // it; when the typing stops the editor is handed back exactly as the user expects to
    // find it.
    private func typeIn(_ text: String) {
        typingTimer?.invalidate()
        program.text = ""
        programChanged()
        program.isEditable = false

        let characters = Array(text)
        var typed = 0
        typingTimer = Timer.scheduledTimer(withTimeInterval: 0.04, repeats: true) { [weak self] timer in
            guard let self else { timer.invalidate(); return }
            guard typed < characters.count else {
                timer.invalidate()
                self.typingTimer = nil
                self.program.isEditable = true
                return
            }
            self.program.text.append(characters[typed])
            typed += 1
            // Per character, so the colouring and the line numbers arrive with the text
            // rather than snapping into place at the end.
            self.programChanged()
        }
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
        // The timer holds this controller only weakly, but a timer still firing into a
        // screen the user has left is work nobody asked for.
        typingTimer?.invalidate()
    }

    // Four levels, in the conventional colours: a reader should not have to learn a
    // private scheme to tell a result from a complaint. System is the odd one - it is not
    // about the program at all, so it is the quietest thing in the pane: grey, at 3.9:1
    // where a program's own output has 15:1, said once and not competing to be read.
    enum ConsoleStyle {
        case system, output, warning, error

        var color: UIColor {
            switch self {
            case .system:  return UIColor(white: 0.45, alpha: 1)
            case .output:  return UIColor(white: 0.10, alpha: 1)
            case .warning: return UIColor(displayP3Red: 0.70, green: 0.42, blue: 0.0, alpha: 1)
            case .error:   return UIColor(displayP3Red: 0.75, green: 0.0, blue: 0.05, alpha: 1)
            }
        }
    }

    // Printed at the head of every run. The flag is a two-scalar regional indicator pair,
    // written in escapes rather than pasted so the source stays ASCII like the language
    // it hosts. The blank line at the end is what separates it from the program's own
    // first line of output.
    static let banner = """
    \u{1F1F5}\u{1F1F0} Shalimar
    ©2019-26 G R Akhtar, Islamabad
    All rights reserved


    """

    // The console is a third of the page and most of a session does not need it. It has
    // nothing to say until a run, and once the output has been read it is a third of the
    // editor spent on text already looked at. The bar between the two panes hands that
    // third back: one tap and the editor grows into the whole page, another and the console
    // returns to the share the storyboard gave it.
    //
    // Not a gesture on the console itself. That view already answers a single tap by
    // copying a word into the editor and a double tap by filling an empty one, and a third
    // meaning on the same surface would leave all three guessing. The bar is its own
    // control and this is the only thing it does.
    private let consoleHandle = ConsoleHandle()
    // The storyboard's 2:1 split, and the constraint that overrules it when the pane is
    // shut. Exactly one is ever active.
    private var consoleShare: NSLayoutConstraint?
    private var consoleCollapsed: NSLayoutConstraint?
    private var isConsoleHidden = false

    private func installConsoleHandle() {
        // The storyboard puts 8pt of nothing between the editor and the console. The bar
        // takes that gap over and 16pt besides, which is the whole cost of this in editor
        // height - about one line - against the third that one tap on it returns.
        // Both of the storyboard's own constraints here were said about the text view, and
        // installEditorPane has already taken them off it - 'consoleShare' replaced by one
        // against the pane, 'consoleGap' simply gone, which is what this line wanted anyway.
        view.constraints.first { $0.identifier == "consoleGap" }?.isActive = false

        consoleHandle.translatesAutoresizingMaskIntoConstraints = false
        consoleHandle.isAccessibilityElement = true
        consoleHandle.accessibilityTraits = .button
        consoleHandle.accessibilityLabel = "Console"

        // The grabber a sheet uses, at the size a sheet uses it: this is a pane that pulls
        // shut, and anyone who has closed a sheet already knows what the pill means without
        // being told. In the gutter's grey, so it reads as furniture beside the code rather
        // than as a fourth signal next to output, warning and error.
        let grip = UIView()
        grip.translatesAutoresizingMaskIntoConstraints = false
        grip.backgroundColor = UIColor(white: 0.6, alpha: 1)
        grip.layer.cornerRadius = 2.5
        consoleHandle.addSubview(grip)

        view.addSubview(consoleHandle)
        NSLayoutConstraint.activate([
            consoleHandle.topAnchor.constraint(equalTo: editorPane.bottomAnchor),
            consoleHandle.bottomAnchor.constraint(equalTo: console.topAnchor),
            consoleHandle.heightAnchor.constraint(equalToConstant: 24),
            // The console's own margins, not the editor's: the bar belongs to the pane it
            // closes, and lining it up with the wider editor would read as a rule across
            // the page.
            consoleHandle.leadingAnchor.constraint(equalTo: console.leadingAnchor),
            consoleHandle.trailingAnchor.constraint(equalTo: console.trailingAnchor),
            grip.centerXAnchor.constraint(equalTo: consoleHandle.centerXAnchor),
            grip.centerYAnchor.constraint(equalTo: consoleHandle.centerYAnchor),
            grip.widthAnchor.constraint(equalToConstant: 40),
            grip.heightAnchor.constraint(equalToConstant: 5)
        ])

        // Built once and left inactive, so a tap flips two flags rather than making a
        // constraint and throwing it away again.
        consoleCollapsed = console.heightAnchor.constraint(equalToConstant: 0)

        consoleHandle.addGestureRecognizer(UITapGestureRecognizer(target: self,
                                                                  action: #selector(consoleHandleTapped)))
        describeConsoleHandle()
    }

    @objc private func consoleHandleTapped() {
        setConsoleHidden(!isConsoleHidden, animated: true)
    }

    // Deactivate before activating, both ways round. For any moment where both are on, the
    // two constraints disagree about the console's height, and Auto Layout settles that by
    // breaking one of them - a wall of console text in the log, and not our choice of which.
    private func setConsoleHidden(_ hidden: Bool, animated: Bool) {
        guard hidden != isConsoleHidden else { return }
        isConsoleHidden = hidden
        if hidden {
            consoleShare?.isActive = false
            consoleCollapsed?.isActive = true
        } else {
            consoleCollapsed?.isActive = false
            consoleShare?.isActive = true
        }
        describeConsoleHandle()

        // Resizing a UITextView scrolls it to wherever its selection is, and after a file
        // is loaded that selection sits at the end of the program - so the first tap threw
        // a reader who was at line 1 down to line 54. Taking the room back is meant to show
        // more of the same page, not a different one. Read the offset before the pane moves
        // and put it back after.
        let reading = editorPane.contentOffset

        // The editor is what moves, and it should look pushed rather than cut: a spring
        // damped just short of a bounce, over about the length of a sheet dismissal. The
        // layout pass has to happen inside the block - that is what gives the constraint
        // change something to travel over, and without it the panes jump.
        guard animated else {
            view.layoutIfNeeded()
            keepEditorAt(reading)
            return
        }
        UIView.animate(withDuration: 0.3, delay: 0, usingSpringWithDamping: 0.85,
                       initialSpringVelocity: 0, options: [.curveEaseInOut]) {
            self.view.layoutIfNeeded()
            self.keepEditorAt(reading)
        }
    }

    // Only after the layout pass, so the bounds and the wrapping are the new ones. Clamped
    // rather than assigned: showing the console shortens the editor, and the offset that
    // was legal in a taller frame can be past the end of a shorter one - which would leave
    // it parked below the last line with nothing to look at.
    private func keepEditorAt(_ offset: CGPoint) {
        let inset = editorPane.adjustedContentInset
        let lowest = max(-inset.top,
                         editorPane.contentSize.height + inset.bottom - editorPane.bounds.height)
        editorPane.contentOffset = CGPoint(x: offset.x, y: min(offset.y, lowest))
    }

    // VoiceOver cannot see that the pane went away, so the bar has to say which of the two
    // things it is about to do.
    private func describeConsoleHandle() {
        consoleHandle.accessibilityValue = isConsoleHidden ? "Hidden" : "Shown"
        consoleHandle.accessibilityHint = isConsoleHidden ? "Shows the output pane"
                                                          : "Hides the output pane"
    }

    private func clearConsole() {
        console.attributedText = NSAttributedString(string: "")
        errorLines = []
        layoutErrorStrips()
    }

    private func append(_ text: String, _ style: ConsoleStyle) {
        let font = console.font ?? UIFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        let run = NSMutableAttributedString(string: text,
                                            attributes: [.foregroundColor: style.color, .font: font])
        Self.enlargeCopyrightSign(in: run, over: font)
        let all = NSMutableAttributedString(attributedString: console.attributedText
                                            ?? NSAttributedString(string: ""))
        all.append(run)
        console.attributedText = all
        if style == .error, let line = Self.reportedLine(in: text) { errorLines.insert(line) }
    }

    // © is drawn inside the same advance as a letter, so its ring ends up around the size
    // of an 'o' counter rather than of the capitals beside it, and at 12pt that reads as a
    // speck. Four points bigger brings the ring up to the cap height of the line it sits
    // in; the baseline offset puts back the drop that setting a taller font on one glyph
    // would otherwise cause, so the text either side stays on its line.
    private static func enlargeCopyrightSign(in run: NSMutableAttributedString, over font: UIFont) {
        let text = run.string as NSString
        let bigger = font.withSize(font.pointSize + 4)
        var searched = NSRange(location: 0, length: text.length)
        while true {
            let found = text.range(of: "©", options: [], range: searched)
            guard found.location != NSNotFound else { return }
            run.addAttributes([.font: bigger, .baselineOffset: -1], range: found)
            let next = NSMaxRange(found)
            searched = NSRange(location: next, length: text.length - next)
        }
    }

    // The four stages report errors in four different types - a String from the lexer, a
    // LocatedParseError, a Diagnostic, a RuntimeError - but every one of them prints as
    // "Error: line N:", and the console text is the one place all four meet. Reading the
    // number back out of the message keeps the strip agreeing with what the user is being
    // told: if the console names a line, that line is what gets marked, and a message
    // without a line (a lex error before any line is known) marks nothing.
    private static func reportedLine(in text: String) -> Int? {
        guard let match = text.range(of: "line [0-9]+", options: .regularExpression) else { return nil }
        return Int(text[match].dropFirst("line ".count))
    }

    func textViewDidChangeSelection(_ textView: UITextView) {
        if !program.text.isEmpty { self.count = 0 }
    }

    func textViewDidChange(_ textView: UITextView) {
        guard textView === program else { return }
        undoSentencePeriod()
        programChanged()
        // The page grows a line at a time and the pane does not follow on its own.
        scrollCaretIntoView()
    }

    // Puts back the space the keyboard turned into a full stop. shouldChangeTextIn armed
    // this by noting the index of the space a second space was about to land beside; if that
    // character is now a period, the shortcut wrote it and it goes back.
    //
    // The caret is saved and restored around the edit. The repair happens behind it - one
    // character replaced by one character, earlier in the line - and without this the text
    // view would drag the caret back to the mend and the next keystroke would land there.
    private func undoSentencePeriod() {
        guard let index = periodWatch else { return }
        periodWatch = nil

        guard Indent.isSentencePeriod(at: index, in: (program.text ?? "") as NSString),
              let editRange = Self.textRange(in: program, for: NSRange(location: index, length: 1))
        else { return }

        let caret = program.selectedTextRange
        program.replace(editRange, withText: " ")
        program.selectedTextRange = caret
    }

    // Everything the editor owes its text after that text moves. Both halves read the
    // same characters, so they belong at the same call sites - a highlight without a
    // renumber leaves the column short, and the reverse leaves a new keyword plain.
    private func programChanged() {
        applySyntaxColours()
        refreshLineNumbers()
        // The marks belong to the run that produced them. Once a character moves, line 7
        // is no longer the line the console complained about, so a strip left behind would
        // be pointing at whatever drifted under it.
        errorLines = []
        layoutErrorStrips()
    }

    // Lines the last run reported an error on, and the strips drawn behind them.
    private var errorLines: Set<Int> = []
    private var errorStrips: [UIView] = []

    // Highlighter yellow, the mark a reader already knows: it says "here" without saying
    // anything about severity, which is right when the console beside it is already saying
    // what went wrong. It is the one colour in the editor that belongs to no other level -
    // not the violets of the code, not the mint of the page - so nothing else can be
    // mistaken for it. Bright, but it costs the text nothing: the code holds 4.0:1 on it,
    // which is what it had on the mint, and the keywords 10.6:1.
    static let errorStripColour = UIColor.init(displayP3Red: 1.0, green: 1.0, blue: 0.0, alpha: 1)

    // A strip per reported line, sized from the layout rather than from a line height, so
    // a line that wraps to three rows is covered by one band three rows tall. They go in
    // at index 0, behind the text view's own container view - painted over the text they
    // would wash it out, and as an attribute on the characters they would stop at the last
    // one and leave a ragged right edge instead of a strip.
    private func layoutErrorStrips() {
        errorStrips.forEach { $0.removeFromSuperview() }
        errorStrips = []
        guard !errorLines.isEmpty else { return }

        let source = (program.text ?? "") as NSString
        let layoutManager = program.layoutManager
        layoutManager.ensureLayout(for: program.textContainer)

        for range in Self.characterRanges(of: errorLines, in: source) {
            let glyphs = layoutManager.glyphRange(forCharacterRange: range, actualCharacterRange: nil)
            var box = layoutManager.boundingRect(forGlyphRange: glyphs, in: program.textContainer)
            // An empty line holds no glyphs, so it has no bounding box - its fragment is
            // the only thing that knows where the cursor would sit.
            if box.height == 0 {
                if glyphs.location < layoutManager.numberOfGlyphs {
                    box = layoutManager.lineFragmentRect(forGlyphAt: glyphs.location, effectiveRange: nil)
                } else if layoutManager.extraLineFragmentTextContainer != nil {
                    box = layoutManager.extraLineFragmentRect
                } else {
                    continue
                }
            }

            let strip = UIView(frame: CGRect(x: 0,
                                             y: box.minY + program.textContainerInset.top,
                                             // The page's full width, not the window's: a
                                             // strip cut to what is on screen would end
                                             // mid-row the moment the page was scrolled.
                                             width: program.bounds.width,
                                             height: box.height))
            strip.backgroundColor = Self.errorStripColour
            strip.isUserInteractionEnabled = false
            program.insertSubview(strip, at: 0)
            errorStrips.append(strip)
        }
    }

    // The character range of each wanted line, walked once rather than by splitting the
    // whole program into an array of lines for the one or two that are wanted.
    private static func characterRanges(of lines: Set<Int>, in source: NSString) -> [NSRange] {
        var ranges: [NSRange] = []
        var line = 1
        var start = 0
        var i = 0
        while i <= source.length {
            if i == source.length || source.character(at: i) == 0x0A {
                if lines.contains(line) {
                    ranges.append(NSRange(location: start, length: i - start))
                }
                line += 1
                start = i + 1
            }
            i += 1
        }
        return ranges
    }

    // Three levels, and the order of them is the point: keywords darkest, code in the
    // middle, comments palest. Violet for code, the same hue driven down near indigo for
    // the words the language reserves - a different hue would have made the keywords a
    // second subject competing with the program, where this reads as emphasis inside one
    // voice. Against the mint they measure 10.7:1, 4.1:1 and 2.7:1, so the eye meets them
    // in that order and a comment is the last thing it stops on.
    static let codeColour = UIColor.init(displayP3Red: 0.63, green: 0.35, blue: 0.75, alpha: 1)
    static let keywordColour = UIColor.init(displayP3Red: 0.32, green: 0.08, blue: 0.52, alpha: 1)
    // Grey, and neutral rather than a paled violet: a comment is not weak code, it is not
    // code at all, and dropping the hue says that more plainly than lightening it would.
    // 2.7:1 is quiet but still legible - the reader who goes looking can read it.
    static let commentColour = UIColor.init(white: 0.6, alpha: 1)
    // The one place a hue change is earned: a literal is the user's own data passing
    // through, not language, so depth alone cannot say what it is. Rose - a pink deep
    // enough to be read, which a pale one is not: at 5.1:1 it sits between the code and
    // the keywords, so a literal announces itself inside the line rather than blending
    // into it. Lightening it is where it stops working - the same pink as a pastel falls
    // to 1.6:1, fainter than the comments it is supposed to outrank.
    static let stringColour = UIColor.init(displayP3Red: 0.75, green: 0.10, blue: 0.38, alpha: 1)

    // Lowercased, because the lexer folds case before it compares (TokenKind.swift) - so
    // WHILE is the keyword too, and colouring only "while" would tell the reader that the
    // capital one is an identifier when the language disagrees.
    // No `elseif`. The language dropped it outright rather than keeping it
    // reserved, so it is an ordinary name now and colouring it would tell the
    // reader the opposite of what the lexer does.
    static let keywords: Set<String> = [
        "if", "else", "while", "for", "to", "step", "fun", "return", "uses",
        "break", "continue", "int", "real", "char"
    ]

    // The colouring has to agree with the lexer about what a word is, or it teaches the
    // wrong thing: "for" inside a comment is prose, and "to" inside a string is data.
    // So this walks the text the way tokenList does rather than matching \bfor\b - "//"
    // to end of line, a quote to the next quote (the lexer's string pattern has no escapes
    // and does not stop at a newline), and identifiers only in what is left over.
    private func applySyntaxColours() {
        let storage = program.textStorage
        let source = storage.string as NSString
        let whole = NSRange(location: 0, length: source.length)
        let body = program.font ?? UIFont.monospacedSystemFont(ofSize: 14, weight: .medium)
        // Weight is free here in a way it would not be in a proportional face: the
        // monospaced system font advances every glyph the same distance at every weight,
        // measured identical from medium to bold. So bold cannot rewrap a line or shift
        // a number off the line it counts - it only makes the word heavier.
        let heavy = UIFont.monospacedSystemFont(ofSize: body.pointSize, weight: .bold)

        storage.beginEditing()
        storage.addAttribute(.foregroundColor, value: Self.codeColour, range: whole)
        storage.addAttribute(.font, value: body, range: whole)

        var i = 0
        while i < source.length {
            let c = source.character(at: i)
            // "//" - the rest of the line is a comment. The lexer throws these away
            // before the parser ever sees them, and the grey says so.
            if c == 0x2F, i + 1 < source.length, source.character(at: i + 1) == 0x2F {
                let start = i
                while i < source.length, source.character(at: i) != 0x0A { i += 1 }
                storage.addAttribute(.foregroundColor, value: Self.commentColour,
                                     range: NSRange(location: start, length: i - start))
                continue
            }
            // A quote opens a string that runs to the next quote on the same line, which
            // is exactly as far as the lexer will take it. Quotes and all: the delimiters
            // are part of the literal, and colouring the body alone would leave them
            // looking like stray operators.
            if c == 0x22 {
                let start = i
                i += 1
                while i < source.length,
                      source.character(at: i) != 0x22,
                      source.character(at: i) != 0x0A { i += 1 }
                i += 1
                // A quote the user has opened and not yet closed colours to the end of its
                // line and no further. Stopping at the newline is what keeps a half-typed
                // literal from painting the rest of the program pink until it is finished.
                let end = min(i, source.length)
                storage.addAttribute(.foregroundColor, value: Self.stringColour,
                                     range: NSRange(location: start, length: end - start))
                continue
            }
            if Self.isIdentifierHead(c) {
                let start = i
                while i < source.length, Self.isIdentifierBody(source.character(at: i)) { i += 1 }
                let range = NSRange(location: start, length: i - start)
                if Self.keywords.contains(source.substring(with: range).lowercased()) {
                    storage.addAttribute(.foregroundColor, value: Self.keywordColour, range: range)
                    storage.addAttribute(.font, value: heavy, range: range)
                }
                continue
            }
            i += 1
        }
        storage.endEditing()

        // Without this the next character typed inherits whatever the caret was sitting
        // in, so a word typed after a keyword starts out keyword-coloured until the next
        // pass repaints it - a flicker on every keystroke at the end of a line.
        program.typingAttributes = [.font: body, .foregroundColor: Self.codeColour]
    }

    // [a-zA-Z_] then [a-zA-Z0-9_]*, which is the identifier pattern in tokenList.
    private static func isIdentifierHead(_ c: unichar) -> Bool {
        (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A) || c == 0x5F
    }

    private static func isIdentifierBody(_ c: unichar) -> Bool {
        isIdentifierHead(c) || (c >= 0x30 && c <= 0x39)
    }

    // The gutter used to be a fixed 1...99, printed once at load. That was wrong in both
    // directions: it ran out on a hundredth line, and on a five-line program it counted
    // 94 lines that were not there. It is generated from the editor now, so the last
    // number in the column is always the last line of the program.
    //
    // Counting "\n" would be enough for the length, but not for the alignment: a line
    // too long for the column occupies two rows on screen while remaining one line to
    // the lexer. Numbering rows would then report the wrong line for every error below
    // the first wrap. So the numbers come from the layout instead - one row per line
    // fragment, but a number only on the fragment that begins a line, and a blank on
    // each continuation. A wrapped line keeps exactly one number, opposite its head.
    private func refreshLineNumbers() {
        let source = (program.text ?? "") as NSString
        let layoutManager = program.layoutManager
        // The fragments below are only as current as the layout, and an edit that has
        // not been laid out yet would be counted at its old width.
        layoutManager.ensureLayout(for: program.textContainer)

        var column: [String] = []
        var line = 0
        let everything = NSRange(location: 0, length: layoutManager.numberOfGlyphs)
        layoutManager.enumerateLineFragments(forGlyphRange: everything) { _, _, _, glyphRange, _ in
            let characters = layoutManager.characterRange(forGlyphRange: glyphRange,
                                                          actualGlyphRange: nil)
            // A fragment begins a line when the character before it is the newline that
            // ended the previous one - or when there is no character before it at all.
            let startsLine = characters.location == 0
                || source.character(at: characters.location - 1) == 0x0A
            if startsLine {
                line += 1
                column.append(String(line))
            } else {
                column.append("")
            }
        }

        // Text ending in a newline, and empty text, both leave a final empty line that
        // holds no glyphs - it is where the cursor sits after Return. TextKit keeps it
        // out of the enumeration above and in the extra fragment instead, but it is a
        // line the user can type on and it needs a number like any other.
        if layoutManager.extraLineFragmentTextContainer != nil {
            line += 1
            column.append(String(line))
        }

        let numbers = column.joined(separator: "\n")
        guard numbers != lineview.text else { return }
        lineview.text = numbers
        // Replacing the text resets the gutter's scroll position, so put it back in step
        // with the editor rather than waiting for the next scroll to do it. Vertically
        // only, for the reason in scrollViewDidScroll.
        lineview.contentOffset = CGPoint(x: 0, y: editorPane.contentOffset.y)
    }

    // NSRange offsets are UTF-16, so the document is measured as NSString throughout
    // rather than by Character - the two disagree the moment anything non-ASCII lands in
    // the buffer, and the keyboard traits do not guarantee it cannot.
    private static func textRange(in textView: UITextView, for range: NSRange) -> UITextRange? {
        guard let start = textView.position(from: textView.beginningOfDocument, offset: range.location),
              let end = textView.position(from: start, offset: range.length) else { return nil }
        return textView.textRange(from: start, to: end)
    }

    // The keyboard traits set in viewDidLoad are only a hint - the globe key and pasting
    // both bypass them - so this is where ASCII is actually enforced. It shares the
    // scanner's substitution table, so text that is typed, pasted, or scanned all end up
    // as the same ASCII source rather than a lookalike that fails at run time.
    func textView(_ textView: UITextView, shouldChangeTextIn range: NSRange, replacementText text: String) -> Bool {
        guard textView === program else { return true }

        // The keyboard's "." shortcut cannot be refused here - it does not pass through this
        // method at all - so what is done instead is to note where it is about to strike and
        // undo it in textViewDidChange, once the character it wrote is there to be read.
        periodWatch = Indent.spaceAtRiskOfPeriod(in: (textView.text ?? "") as NSString,
                                                 replacing: range,
                                                 with: text)

        // Enter carries the brace depth onto the new line, and a } typed as the first
        // thing on a line pulls that line back one level. Between them the text stays
        // laid out as it is typed, so reindenting is only needed on the way in.
        if text == "\n" || text == "}" {
            if let indented = Indent.insertion(of: text,
                                              into: (textView.text ?? "") as NSString,
                                              replacing: range),
               let editRange = Self.textRange(in: textView, for: indented.range) {
                textView.replace(editRange, withText: indented.text)
                return false
            }
            return true
        }

        let normalized = Self.normalizedToASCII(text)

        // A paste is laid out at the level it lands in rather than keeping the indentation
        // it was copied with - six columns of it, when it comes from the reference. It is
        // normalized first, so what gets laid out is the ASCII that will actually be
        // inserted and not a lookalike that would move the braces this counts.
        if let laidOut = Indent.paste(of: normalized,
                                      into: (textView.text ?? "") as NSString,
                                      replacing: range),
           let editRange = Self.textRange(in: textView, for: laidOut.range) {
            textView.replace(editRange, withText: laidOut.text)
            return false
        }

        guard normalized != text else { return true }

        // Insert the cleaned text and reject the original edit. This cannot recurse:
        // normalized text holds no confusables, so the guard above short-circuits it.
        if let start = textView.position(from: textView.beginningOfDocument, offset: range.location),
           let end = textView.position(from: start, offset: range.length),
           let editRange = textView.textRange(from: start, to: end) {
            textView.replace(editRange, withText: normalized)
            return false
        }
        return true
    }
    
    // Double-tapping the console fills an empty editor with something that runs.
    //
    // It used to build that from the console's own text, taking lines 5, 6, 7, 18 and 8
    // by number. Those were positions inside the template sheet that was printed into the
    // console at launch; once nothing printed it, line 18 was usually not there at all and
    // the subscript trapped - and on the runs where the console did have 19 lines, it
    // spliced five unrelated lines of program output into the editor and called it a
    // program. Both failures come from addressing text by line number that was never
    // promised to be there.
    //
    // The same starting program the editor opens with is what it seeds now: fixed, known
    // to parse, and owing nothing to whatever the console happens to be showing.
    @objc func doubleTapped() {
        guard program.text.isEmpty else { return }
        typeIn(Indent.reindented(source))
    }
    
    // One way only. This fires for the gutter's own scrolling too, and answering that by
    // reassigning the gutter's offset is a loop that reports as a twitch on screen.
    // Vertically only. The pane moves on both axes, but the gutter is one column of numbers
    // with no width to spare: handed the pane's x as well, the numbers would slide out of
    // their own view as soon as a long line was followed to the right, and the code would be
    // left counting against a blank strip.
    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        guard scrollView === editorPane else { return }
        lineview.contentOffset = CGPoint(x: 0, y: editorPane.contentOffset.y)
    }
    
    @IBAction func ComputeTapped(_ sender: Any) {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
        guard let programSource = program.text else {return}

        // A run whose output has nowhere to land is a run nobody can read, and the errors
        // would be worse than the output: a program that failed to parse would show as
        // yellow strips in the editor and no word anywhere about what was wrong with them.
        // Starting a program is a request to see what it does, so the pane comes back on
        // its own - the tap that shut it did not mean "and keep it shut through a run".
        setConsoleHidden(false, animated: true)

        clearConsole()
        append(Self.banner, .system)
        // Every stage below can return early, and the strips have to be drawn whichever
        // one stopped the run - so this is deferred rather than repeated at four exits.
        defer { layoutErrorStrips() }

        let lexer = Lexer(input: programSource)
        let tokens = lexer.tokenize()

        // Must come before parsing: tokenize() stops at the offending character, so the
        // token stream is truncated and any parse error from here would point elsewhere.
        if let lexError = lexer.lexError {
            append("\(lexError)\n", .error)
            return
        }

        let parser = Parser(tokens: tokens)
        let astNodes = parser.parseProgram()

        if let parseError = parser.parseError {
            append("\(parseError)\n", .error)
            return
        }

        // The 3.0 stage between parsing and running. It validates every call, return and
        // reference argument against the prototype that owns it, resolves every name, and
        // hands back a rewritten AST in which each implicit conversion is a ConvertNode -
        // so the interpreter does no inference of its own.
        let checker = Checker(borrowing: parser.borrowed)
        let checkedAST = checker.check(astNodes)

        // Warnings show either way; only errors stop the run. Unlike the other two stages
        // this one does not stop at the first problem, so the console lists them all.
        for diagnostic in checker.diagnostics {
            append("\(diagnostic)\n", diagnostic.severity == .error ? .error : .warning)
        }
        if checker.hasErrors { return }

        let interpreter = Interpreter()
        interpreter.diagnostic = { [weak self] text in self?.append(text, .error) }
        interpreter.output = { [weak self] text in
            self?.append(text, .output)
        }
        interpreter.run(checkedAST)
    }

    // Pushed rather than presented so the editor keeps its place underneath and the
    // automatic back button returns to exactly the program being written.
    @objc func helpTapped() {
        navigationController?.pushViewController(HelpViewController(), animated: true)
    }

    @objc func scanTapped() {
        guard VNDocumentCameraViewController.isSupported else {
            let a = UIAlertController(title: "Scan Unavailable",
                message: "Document scanning isn't supported on this device.", preferredStyle: .alert)
            a.addAction(UIAlertAction(title: "Ok", style: .default, handler: nil))
            present(a, animated: true, completion: nil)
            return
        }

        let scanner = VNDocumentCameraViewController()
        scanner.delegate = self
        present(scanner, animated: true, completion: nil)
    }

    func documentCameraViewController(_ controller: VNDocumentCameraViewController,
                                       didFinishWith scan: VNDocumentCameraScan) {
        let pageImages = (0..<scan.pageCount).map { scan.imageOfPage(at: $0) }
        controller.dismiss(animated: true) { [weak self] in
            self?.recognizeText(in: pageImages)
        }
    }

    func documentCameraViewControllerDidCancel(_ controller: VNDocumentCameraViewController) {
        controller.dismiss(animated: true, completion: nil)
    }

    func documentCameraViewController(_ controller: VNDocumentCameraViewController, didFailWithError error: Error) {
        controller.dismiss(animated: true) { [weak self] in
            let a = UIAlertController(title: "Scan Failed", message: "\(error)", preferredStyle: .alert)
            a.addAction(UIAlertAction(title: "Ok", style: .default, handler: nil))
            self?.present(a, animated: true, completion: nil)
        }
    }

    private func recognizeText(in pageImages: [UIImage]) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            var allLines: [String] = []
            for image in pageImages {
                guard let cgImage = image.cgImage else { continue }
                let request = VNRecognizeTextRequest()
                request.recognitionLevel = .accurate
                request.usesLanguageCorrection = false // code, not prose - don't autocorrect it
                request.recognitionLanguages = ["en-US"]
                if #available(iOS 16.0, *) {
                    request.automaticallyDetectsLanguage = false
                }

                let handler = VNImageRequestHandler(cgImage: cgImage, options: [:])
                do {
                    try handler.perform([request])

                    // Vision doesn't guarantee reading order, doesn't promise one observation per
                    // printed line, and strips leading whitespace from each string. ScanLayout
                    // rebuilds lines, order and indentation from the bounding boxes - which line a
                    // piece of text belongs to now changes what the program means, so it cannot be
                    // left to the order Vision happened to return (see ScanLayout.swift).
                    let fragments: [ScanLayout.Fragment] = (request.results ?? []).compactMap {
                        guard let text = $0.topCandidates(1).first?.string, !text.isEmpty else { return nil }
                        return ScanLayout.Fragment(text: Self.normalizedToASCII(text), box: $0.boundingBox)
                    }
                    allLines += ScanLayout.lines(from: fragments)
                } catch {
                    print(error)
                }
            }

            let programLines = Self.trimToProgramBody(allLines)
            let scannedText = programLines.joined(separator: "\n")
            // A print command that isn't first on its line is a parse error the moment this runs,
            // and after a scan the likeliest cause is two printed lines read as one. Saying so here,
            // while the user is already being asked to check the text, beats letting them hit Run
            // and work backwards from the line number.
            let lateCommands = ScanLayout.linesWithLateCommand(in: programLines)

            DispatchQueue.main.async {
                guard let self = self else { return }
                // Line count is preserved, so the numbers in lateCommands still point at
                // the lines the user is about to be shown.
                self.program.text = Indent.reindented(scannedText)
                self.programChanged()
                var message = "Let's review the code before running."
                if let first = lateCommands.first {
                    message += "\n\nLine \(first) has a '?' that isn't at the start of the line"
                    if lateCommands.count > 1 {
                        message += " (and \(lateCommands.count - 1) more)"
                    }
                    message += ". Two lines may have been scanned as one."
                }
                let a = UIAlertController(
                            title:   "Scan May Be Incomplete",
                            message: message,
                            preferredStyle: .alert)
                a.addAction(UIAlertAction(title: "Ok", style: .default, handler: nil))
                self.present(a, animated: true, completion: nil)
            }
        }
    }

    // OCR routinely substitutes non-ASCII characters that are visually identical to ASCII
    // ones - Cyrillic "х" (U+0445) for Latin "x", a smart quote for ", an en-dash for "-".
    // Shalimar is an ASCII language, so every one of these is a scanning mistake, and they
    // are invisible on screen: the lexer's `char.isLetter` check happily accepts a Cyrillic
    // letter as an identifier character, so "хn" silently becomes a *different* variable
    // than "xn" - producing either wrong results or "Undefined variable" at run time.
    // Only glyphs that are genuinely confusable with ASCII are mapped, so deliberate
    // non-ASCII text (in a string literal, say) is left alone.
    private static let asciiConfusables: [Character: Character] = [
        // Cyrillic lowercase
        "а": "a", "е": "e", "о": "o", "р": "p", "с": "c", "у": "y", "х": "x",
        "і": "i", "ј": "j", "ѕ": "s", "к": "k",
        // Cyrillic uppercase
        "А": "A", "В": "B", "Е": "E", "К": "K", "М": "M", "Н": "H", "О": "O",
        "Р": "P", "С": "C", "Т": "T", "У": "Y", "Х": "X",
        // Greek uppercase
        "Α": "A", "Β": "B", "Ε": "E", "Ζ": "Z", "Η": "H", "Ι": "I", "Κ": "K",
        "Μ": "M", "Ν": "N", "Ο": "O", "Ρ": "P", "Τ": "T", "Υ": "Y", "Χ": "X",
        // Greek lowercase
        "ο": "o", "ρ": "p", "ν": "v", "ι": "i",
        // Punctuation and operators the scanner likes to prettify
        "\u{201C}": "\"", "\u{201D}": "\"", "\u{2018}": "'", "\u{2019}": "'",
        "\u{2013}": "-", "\u{2014}": "-", "\u{2212}": "-",
        "×": "*", "÷": "/", "\u{00A0}": " ",
        "：": ":", "（": "(", "）": ")", "＊": "*", "，": ",",
        // A dot used to appear only inside a numeral, where a wrong glyph produced a loud
        // "Malformed number". Since '.row'/'.col'/'.dim(n)' it also carries syntax, so the
        // full-stop lookalikes have to be folded back to ASCII too.
        "。": ".", "．": ".", "•": "."
    ]

    private static func normalizedToASCII(_ text: String) -> String {
        // 1:1 character substitution - line lengths are preserved, which matters because
        // the indentation math above divides a line's width by its character count.
        String(text.map { asciiConfusables[$0] ?? $0 })
    }

    // Crops scanned lines down to the entry point's block: from `fun <> = main() {`
    // through the `}` that balances it, dropping any page header/footer noise Vision
    // picked up outside the program itself.
    private static func trimToProgramBody(_ lines: [String]) -> [String] {
        guard let startIndex = lines.firstIndex(where: { line in
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            return trimmed.hasPrefix("fun") && trimmed.contains("main(") && trimmed.hasSuffix("{")
        }) else {
            return lines
        }

        var braceBalance = 0
        for i in startIndex..<lines.count {
            for char in lines[i] {
                if char == "{" { braceBalance += 1 }
                else if char == "}" { braceBalance -= 1 }
            }
            if braceBalance <= 0 {
                return Array(lines[startIndex...i])
            }
        }
        return Array(lines[startIndex...])
    }

    @objc private func textTapped(_ tapGesture: UITapGestureRecognizer) {
        let textView = tapGesture.view as? UITextView
        let point = tapGesture.location(in: textView!)
        // A tap below the last line of output finds an empty line, and copying that into
        // the editor added a blank line for every miss.
        if let detectedWord = getWordAtPosition(tapGesture, point),
           !detectedWord.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            if self.count == 0 {
                program.text += detectedWord + "\n"
                programChanged()
            }
        }
    }

    private func getWordAtPosition(_ tapGesture: UITapGestureRecognizer, _ point: CGPoint) -> String? {
        let textView = tapGesture.view as? UITextView
        if let textPosition = textView!.closestPosition(to: point) {
            if let range = textView!.tokenizer.rangeEnclosingPosition(
                textPosition, with: .line, inDirection: UITextDirection(rawValue: 1)) {
                return textView!.text(in: range)
            }
        }

        return nil
    }

    @objc func saveTapped() {
        guard let coordinator = coordinator else { return }
        // An example lives in the app bundle, which is not writable - and must not be, or
        // the baseline the examples exist to hold could be edited away one save at a time.
        // So saving one asks for a name and writes a copy into the user's own programs,
        // exactly as a program typed from scratch does; the example itself is untouched
        // and the editor is now working on the copy.
        if coordinator.fileURL.isEmpty || coordinator.fileIsExample {
            promptForFileName { [weak self] name in
                guard let self = self, let name = name else { return }
                let fileName = Self.named(name)
                // Before the write, so the name is in the file and not only in the editor.
                self.writeNameHeader(fileName)
                self.coordinator?.fileURL = fileName
                self.coordinator?.fileIsExample = false
                self.save(fileName: fileName)
            }
        } else {
            // A stamp that says when the file was saved has to mean the last save, not the
            // first, or it is a date that quietly goes wrong. Only files that already carry
            // one are touched: a program without a header keeps whatever the user wrote at
            // the top of it.
            refreshSavedStamp()
            save(fileName: coordinator.fileURL)
        }
    }

    // The name the user typed, as it will be on disk. One rule, used by the save itself
    // and by the header written into the program, so the two cannot disagree.
    static func named(_ name: String) -> String {
        (name as NSString).pathExtension == "shm" ? name : name + ".shm"
    }

    // A program on disk knows its name; a program on screen does not, and the editor shows
    // no filename anywhere. So the name goes into the source as its first line, the way
    // every file in Examples/ already opens - the program carries its own name from then
    // on, into a scan, a share, or a paste somewhere else.
    //
    // A header already there is replaced rather than stacked, so saving twice does not
    // leave two names at the top of the file. Anything else on the first line is a comment
    // the user wrote, and the name goes above it.
    // The name, then when it was saved. Two lines, written and replaced as a pair, so a
    // second save updates the header rather than stacking another one above it.
    private func writeNameHeader(_ fileName: String) {
        let lines = (program.text ?? "").components(separatedBy: "\n")
        // How much of what is already there is this header's own, and so may be replaced:
        // the name line, and the stamp under it if that is there too. Everything below is
        // the user's and is left where it is.
        var existing = 0
        if let first = lines.first, Self.isNameHeader(first) { existing = 1 }
        if lines.count > existing, Self.isTimeStamp(lines[existing]) { existing += 1 }

        replaceLeadingLines(existing, with: ["// " + fileName, Self.timeStamp()])
    }

    // Re-saving a file that already carries a header: the time changes, the name does not.
    private func refreshSavedStamp() {
        let lines = (program.text ?? "").components(separatedBy: "\n")
        guard let first = lines.first, Self.isNameHeader(first),
              lines.count > 1, Self.isTimeStamp(lines[1]) else { return }
        replaceLeadingLines(2, with: [first, Self.timeStamp()])
    }

    // Both callers above swap whole lines at the top of the program and leave everything
    // under them alone, so both owe the reader the same thing: the page as they left it.
    //
    // Assigning to `text` puts the caret at the end of the document, and the pane follows
    // the caret - so a save made from line 27 of a long program landed the reader on the
    // last line, at the right-hand edge of the page, with nothing on screen they had been
    // looking at. Saving changes the header; it is not a request to go anywhere. The
    // reading position and the caret are read before the swap and put back after it.
    private func replaceLeadingLines(_ count: Int, with header: [String]) {
        var lines = (program.text ?? "").components(separatedBy: "\n")
        let reading = editorPane.contentOffset
        let caret = program.selectedRange
        let was = Self.prefixLength(of: lines.prefix(count))
        let now = Self.prefixLength(of: header[...])

        lines.replaceSubrange(0..<count, with: header)
        program.text = lines.joined(separator: "\n")
        programChanged()

        // A caret below the header stays on the character it was on, however many
        // characters the header gained or lost above it. One inside the header is sitting
        // in text this has just rewritten and has no character to keep, so it goes to the
        // first line under it.
        let length = (program.text as NSString).length
        let moved = caret.location >= was ? caret.location + now - was : now
        program.selectedRange = NSRange(location: min(moved, length), length: 0)

        // Only after the layout pass: the offset is put back against the wrapping and the
        // content height the new text has, not the ones it had a moment ago.
        view.layoutIfNeeded()
        keepEditorAt(reading)
    }

    // How far into the text a run of whole lines reaches, the newline that ends each of
    // them counted. UTF-16, because that is the unit an NSRange location is measured in.
    private static func prefixLength(of lines: ArraySlice<String>) -> Int {
        lines.reduce(0) { $0 + $1.utf16.count + 1 }
    }

    // "March 23, 2026. 7:00 AM" - written out, the way the date at the foot of the
    // reference is. The month is a name rather than a number, which is what keeps 03/04
    // from being read as the wrong day; the POSIX locale pins the English names and the
    // AM/PM, so the line is the same string on a phone set to any calendar or numerals.
    private static func timeStamp() -> String {
        let stamp = DateFormatter()
        stamp.locale = Locale(identifier: "en_US_POSIX")
        stamp.dateFormat = "MMMM d, yyyy. h:mm a"
        return "// " + stamp.string(from: Date())
    }

    // "// something.shm" and nothing else on the line - the shape this writes, so that it
    // recognises its own work and not a comment that merely mentions a file.
    private static func isNameHeader(_ line: String) -> Bool {
        let text = line.trimmingCharacters(in: .whitespaces)
        guard text.hasPrefix("//"), text.hasSuffix(".shm") else { return false }
        let name = text.dropFirst(2).trimmingCharacters(in: .whitespaces)
        return !name.contains(" ") && name.count > ".shm".count
    }

    // Likewise narrow: a comment holding a date in exactly the shape written above and
    // nothing else. With no word in front of it, the pattern is all there is to go on, so
    // it is matched whole - a comment that merely mentions a month is not one of these
    // and is not overwritten.
    private static func isTimeStamp(_ line: String) -> Bool {
        let text = line.trimmingCharacters(in: .whitespaces)
        return text.range(of: "^// [A-Z][a-z]+ [0-9]{1,2}, [0-9]{4}\\. [0-9]{1,2}:[0-9]{2} (AM|PM)$",
                          options: .regularExpression) != nil
    }

    private func promptForFileName(completion: @escaping (String?) -> Void) {
        let alert = UIAlertController(title: "Save Program", message: "Enter a name for this program.", preferredStyle: .alert)
        alert.addTextField { $0.placeholder = "Program name" }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel) { _ in completion(nil) })
        alert.addAction(UIAlertAction(title: "Save", style: .default) { [weak alert] _ in
            let name = alert?.textFields?.first?.text?
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .replacingOccurrences(of: "/", with: "-")
            completion((name?.isEmpty ?? true) ? nil : name)
        })
        present(alert, animated: true, completion: nil)
    }

    func save(fileName: String) {
        do {
            let docDirURL = try FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
            // fileName is already extensioned when re-saving an existing file (coordinator.fileURL
            // is the file's full name as listed on the home screen); named() only appends ".shm"
            // for a brand-new name typed into the save prompt, so re-saving doesn't produce
            // "x.shm.shm".
            let fileURL = docDirURL.appendingPathComponent(Self.named(fileName))
            try program.text.write(to: fileURL, atomically: true, encoding: String.Encoding.utf8)
        } catch let error as NSError {
            print(error)
        }
    }

    func load(fileName: String) {
        do {
            let fileURL: URL
            if coordinator?.fileIsExample == true {
                guard let bundled = BundledExamples.url(for: fileName) else { return }
                fileURL = bundled
            } else {
                let docDirURL = try FileManager.default.url(for: .documentDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
                fileURL = docDirURL.appendingPathComponent(fileName)
            }
            program.text = Indent.reindented(try String(contentsOf: fileURL))
            programChanged()
        } catch let error as NSError {
            print(error)
        }
    }
}

// The bar draws 24pt and a finger wants 44, so it answers for 10pt above and below itself
// as well. Almost all of that is margin the text views were not using: each carries an 8pt
// container inset at the edge it meets here, which leaves 2pt of live text claimed on
// either side. Overriding this rather than growing the view is what keeps the extra reach
// out of the layout - a taller bar would take the same space from the editor whether a
// finger ever arrived there or not.
private final class ConsoleHandle: UIView {
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        bounds.insetBy(dx: 0, dy: -10).contains(point)
    }
}
