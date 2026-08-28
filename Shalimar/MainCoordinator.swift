//
//  MainCoordinator.swift
//  Shalimar
//
//  Created by Home on 5/21/19.
//  Copyright © 2019-2026 G. R. Akhtar. All rights reserved.
//

import Foundation
import UIKit

/// The example programs shipped inside the app.
///
/// They are read-only by construction rather than by a flag anyone has to remember to
/// check: the bundle is not writable on a device at all. That is the whole point of
/// keeping them here instead of seeding them into Documents on first launch - a
/// baseline that can be edited or deleted stops being a baseline, quietly, and the
/// user finds out only when they go looking for the syntax it was meant to preserve.
///
/// Read from the directory rather than from a list in code, so a new program added to
/// `Examples/` appears here without a second edit - the same rule the regression suite
/// follows. The order below names files it already knows, but never decides which exist.
///
/// **Both languages, and the extension is how they are told apart here.** The editor
/// decides what a program is by reading it, not by its name - but a file has to be
/// picked up before it can be read, and `.c` is what a C program is called. It is also
/// what keeps `Tests/regression.sh` right: that suite runs every `Examples/*.shm`
/// through the Shalimar harness, which has no converter in it, so a C program named
/// `.shm` would fail it for being exactly what it is.
enum BundledExamples {
    /// A teaching order, not an alphabetical one: each program needs only what the ones
    /// above it have introduced, so the list can be read straight down. Alphabetical put
    /// factorial first and table fourth, which is a good way to find a program you can
    /// already name and a poor way to meet the language.
    ///
    /// A file not named here still appears - after these, alphabetically - so dropping a
    /// new program into the directory still needs no second edit. It simply lands at the
    /// end until someone decides where it belongs.
    private static let order = [
        "table.shm", "factorial.shm", "gcd.shm", "calendar.c", "fibonacci.shm",
        "quadratic.shm", "prime.shm", "sqroot.shm", "gaussseidel.shm", "strsplit.shm",
        "rotations.shm", "invert.shm", "rotmat.shm",
    ]

    static var directory: URL? {
        Bundle.main.url(forResource: "Examples", withExtension: nil)
    }

    /// How many examples the parent list shows. **Four, and the fourth is the C
    /// one.** Twelve filled the screen and pushed "My programs" - the reader's
    /// own work, and the reason they opened the app - off the top of it; three
    /// fixed that, and a fourth line does not undo it. The rest stay in the
    /// bundle; this shortens the list, it does not delete anything, so raising
    /// the number is the whole of putting them back.
    ///
    /// The C example is fourth rather than first on purpose. The order above is
    /// a teaching order for meeting *Shalimar* - each program needing only what
    /// the ones above it introduced - and a C program is not a step in that.
    /// It sits at the end of what is shown, where it reads as the other thing
    /// this app can do rather than as the way in.
    private static let shown = 4

    static func names() -> [String] {
        guard let directory = directory,
              let files = try? FileManager.default.contentsOfDirectory(
                  at: directory, includingPropertiesForKeys: nil) else { return [] }
        let found = Set(files.filter { $0.pathExtension == "shm" || $0.pathExtension == "c" }
                             .map { $0.lastPathComponent })
        let all = order.filter { found.contains($0) }
                + found.subtracting(order).sorted()
        return Array(all.prefix(shown))
    }

    static func url(for name: String) -> URL? {
        directory?.appendingPathComponent(name)
    }
}

class MainCoordinator: Coordinator {
    var childCoordinators = [Coordinator]()
    var navigationController: UINavigationController
    var fileURL: String

    // Whether fileURL names a program in the bundle rather than one of the user's own.
    // The editor reads from a different place for each, and saving an example writes a
    // copy into Documents instead of writing back.
    var fileIsExample = false

    init(navigationController: UINavigationController) {
        self.navigationController = navigationController
        fileURL = ""
    }
    
    func start() {
        let vc = FirstViewController.instantiate()
        vc.coordinator = self
        navigationController.pushViewController(vc, animated: true)
    }
    
    func compute() {
        let vc = ComputeViewController.instantiate()
        vc.coordinator = self
        navigationController.pushViewController(vc, animated: true)
    }
}
