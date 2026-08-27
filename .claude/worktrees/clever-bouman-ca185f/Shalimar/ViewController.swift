//
//  ViewController.swift
//  Shalimar: G. R. Akhtar
//
//  Created by Home on 5/21/19.
//  Updated by Hone on 9/9/19
//  Copyright © 2019-2026 G. R. Akhtar. All rights reserved.
//

import UIKit

extension FileManager {
    func urls(for directory: FileManager.SearchPathDirectory, skipsHiddenFiles: Bool = true ) -> [URL]? {
        let documentsURL = urls(for: directory, in: .userDomainMask)[0]
        let fileURLs = try? contentsOfDirectory(at: documentsURL, includingPropertiesForKeys: nil, options: skipsHiddenFiles ? .skipsHiddenFiles : [] )
        return fileURLs
    }
}

class FirstViewController: UITableViewController, Storyboarded {
    weak var coordinator: MainCoordinator?
    var option = [String]()

    // The programs that ship with the app. Read once on appearing, like the user's own,
    // so the two lists are built the same way.
    private var examples = [String]()

    private enum Section: Int, CaseIterable {
        case mine = 0, examples = 1
    }
    
    override func viewDidLoad() {
        super.viewDidLoad()

        title = "Shalimar"
        // The app's own name, given the weight of one: 30pt heavy against the 17pt
        // semibold every other screen's title uses. Only this screen - the appearance is
        // set on the navigation item rather than on the bar, so "Shalimar Reference" and
        // the editor's own bar are untouched.
        //
        // Green rather than the yellow the bar sets, and as deep a green as the purple
        // behind it allows: a true dark green measures 1.3:1 there, which is a title you
        // cannot read, where this holds 3.5:1 and still reads as green rather than mint.
        let appearance = UINavigationBarAppearance()
        appearance.configureWithOpaqueBackground()
        appearance.backgroundColor = UIColor.purple
        appearance.titleTextAttributes = [
            .foregroundColor: UIColor(displayP3Red: 0.0, green: 0.70, blue: 0.25, alpha: 1),
            .font: UIFont.systemFont(ofSize: 30, weight: .heavy)
        ]
        navigationItem.standardAppearance = appearance
        navigationItem.scrollEdgeAppearance = appearance
        navigationItem.compactAppearance = appearance

        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .add, target: self, action: #selector(newProgramTapped))
    }

    @objc func newProgramTapped() {
        coordinator?.fileURL = ""
        coordinator?.fileIsExample = false
        coordinator?.compute()
    }
    
    override func viewDidAppear(_ animated: Bool) {
        option.removeAll()

        let list:[URL] = FileManager.default.urls(for: .documentDirectory)!
        for index in list.indices {
            let name = list[index].lastPathComponent
            if(!option.contains(name)) {
                option.append(name)
            }
        }
        examples = BundledExamples.names()
        // No background empty state any more: the examples section is never empty, so the
        // screen can no longer look like one that failed to load. The "nothing here yet"
        // message belongs to the user's own section and is shown as a row inside it.
        tableView.backgroundView = nil
        tableView.reloadData()
    }

    // Every row is a program the user saved, so before they have saved one there is
    // nothing here at all - and a blank white screen under a title reads as a screen that
    // failed to load rather than one that is waiting. The label says where the programs
    // will come from and points at the one control on the screen that makes one.
    override func numberOfSections(in tableView: UITableView) -> Int {
        return Section.allCases.count
    }

    override func tableView(_ tableView: UITableView, titleForHeaderInSection section: Int) -> String? {
        switch Section(rawValue: section) {
        case .mine:     return "My programs"
        // Said on the header rather than on every row: it is a property of the whole
        // section, and repeating it twelve times would read as a warning rather than a
        // label.
        case .examples: return examples.isEmpty ? nil : "Examples (read-only)"
        case .none:     return nil
        }
    }

    // No footer explaining the rule at length: a plain-style table gives a section footer
    // one sticky line, which truncated the sentence mid-word. The header carries the whole
    // of what the user needs before opening one, and saving an example asks for a name,
    // which says the rest at the moment it matters.

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        switch Section(rawValue: section) {
        // One row carrying the "nothing here yet" message, so the section still has a
        // shape before the user has saved anything.
        case .mine:     return max(option.count, 1)
        case .examples: return examples.count
        case .none:     return 0
        }
    }

    /// True for the placeholder row standing in for an empty "My programs".
    private func isPlaceholder(_ indexPath: IndexPath) -> Bool {
        Section(rawValue: indexPath.section) == .mine && option.isEmpty
    }

    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "Cell", for: indexPath) as UITableViewCell

        cell.textLabel?.textColor = UIColor.label
        cell.selectionStyle = .default
        cell.accessoryType = .disclosureIndicator

        if isPlaceholder(indexPath) {
            cell.textLabel?.text = "No programs yet - tap + to write one."
            cell.textLabel?.textColor = UIColor.secondaryLabel
            cell.selectionStyle = .none
            cell.accessoryType = .none
            return cell
        }

        switch Section(rawValue: indexPath.section) {
        case .examples: cell.textLabel?.text = examples[indexPath.row]
        default:        cell.textLabel?.text = option[indexPath.row]
        }
        return cell
    }

    override func tableView(_ tableView: UITableView, didUnhighlightRowAt indexPath: IndexPath) {
        guard !isPlaceholder(indexPath) else { return }

        if Section(rawValue: indexPath.section) == .examples {
            coordinator?.fileURL = examples[indexPath.row]
            coordinator?.fileIsExample = true
        } else {
            coordinator?.fileURL = option[indexPath.row]
            coordinator?.fileIsExample = false
        }
        coordinator?.compute()
    }

    // Only the user's own programs can be deleted. An example is in the bundle, where
    // the delete would fail anyway - refusing the swipe says so before it is attempted.
    override func tableView(_ tableView: UITableView, trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath) -> UISwipeActionsConfiguration? {
        guard Section(rawValue: indexPath.section) == .mine, !isPlaceholder(indexPath) else { return nil }
        let delete = deleteAction(at: indexPath)
        return UISwipeActionsConfiguration(actions: [delete])
    }

    func deleteAction(at indexPath:IndexPath) -> UIContextualAction {
        let action = UIContextualAction(style: .destructive, title: "Delete") {
            (action, view, completion) in
            
            if  self.deleteItem(urlName: self.option[indexPath.row]) {
                self.option.remove(at: indexPath.row)
                // Deleting the last one leaves the section holding its placeholder row
                // rather than no rows, so the row is replaced instead of removed.
                if self.option.isEmpty {
                    self.tableView.reloadSections([Section.mine.rawValue], with: .automatic)
                } else {
                    self.tableView.deleteRows(at: [indexPath], with: .automatic)
                }
            }
            completion(true)
        }
        
        return action
    }

    func deleteItem(urlName: String) -> Bool {
        let list:[URL] = FileManager.default.urls(for: .documentDirectory)!
        for index in list.indices {
            let url: URL = list[index]
            let name = url.lastPathComponent
            if( name.contains(urlName)) {
                self.removeThisURL(url: url)
                return true
            }
        }
        return false
    }

    func removeThisURL(url: URL) {
        do {
            try FileManager.default.removeItem(at: url)
        } catch  let error as NSError {
            showAlert(title: navigationItem.title!, message: error.localizedFailureReason!)
        }
    }

    func showAlert(title: String, message: String, style: UIAlertController.Style = .alert) {
        let alertController = UIAlertController(title: title, message: message, preferredStyle: style)
        let action = UIAlertAction(title: "OK", style: .default) { (action) in
            self.dismiss(animated: true, completion: nil)
        }
        alertController.addAction(action)
        self.present(alertController, animated: true, completion: nil)
    }
/*
    func askingAlert(title: String, message: String, style: UIAlertController.Style = .alert) -> Bool {
        var resp:Bool = false
        let alertController = UIAlertController(title: title, message: message, preferredStyle: style)
        let actionY = UIAlertAction(title: "Yes", style: .default) { (action) in
            self.dismiss(animated: true, completion: nil)
            resp = true
        }
        let actionN = UIAlertAction(title: "No", style: .default) { (action) in
            self.dismiss(animated: true, completion: nil)
        }
        alertController.addAction(actionY)
        alertController.addAction(actionN)
        
        self.present(alertController, animated: true, completion: nil)
        return resp
    } */
}
