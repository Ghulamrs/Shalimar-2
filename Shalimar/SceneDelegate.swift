//
//  SceneDelegate.swift
//  Shalimar: G. R. Akhtar
//
//  Created by Home on 1/08/26.
//  Copyright © 2019-2026 G. R. Akhtar. All rights reserved.
//

import UIKit

class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?
    var coordinator: MainCoordinator?

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }

        let navController = UINavigationController()
        coordinator = MainCoordinator(navigationController: navController)
        coordinator?.start()

        // iOS 15+ splits nav bar styling into separate "standard" and "scroll edge" appearances -
        // a screen whose content starts right at the top (like our table view, or a text view
        // scrolled to the top) uses scrollEdgeAppearance, which defaults to a transparent look
        // unless configured. Setting only the legacy barTintColor/tintColor properties leaves
        // scrollEdgeAppearance unconfigured, which is exactly why different screens looked
        // inconsistent. Configuring one UINavigationBarAppearance and applying it everywhere
        // guarantees every screen, in every scroll state, matches.
        let appearance = UINavigationBarAppearance()
        appearance.configureWithOpaqueBackground()
        appearance.backgroundColor = UIColor.purple
        appearance.titleTextAttributes = [.foregroundColor: UIColor.yellow]
        appearance.largeTitleTextAttributes = [.foregroundColor: UIColor.yellow]

        let bar = navController.navigationBar
        bar.standardAppearance = appearance
        bar.scrollEdgeAppearance = appearance
        bar.compactAppearance = appearance
        if #available(iOS 15.0, *) {
            bar.compactScrollEdgeAppearance = appearance
        }
        bar.tintColor = UIColor.yellow

        window = UIWindow(windowScene: windowScene)
        window?.rootViewController = navController
        window?.makeKeyAndVisible()

    }
}
