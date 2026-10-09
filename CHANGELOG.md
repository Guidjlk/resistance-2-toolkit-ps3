# Changelog

Native PS3 application history. Windows toolkit changes are tracked separately. These versions retain the same application identity and automatic-backup format.

## 0.1.9

- Moved unlock status labels into a right-aligned column beside each name, inside the selection highlight.
- Changed **Saved on first apply** to **Saves on first apply**.
- Grayed out Revert to Original and blocked selection/confirmation when no valid backup is available.
- Added an explanation when the unavailable Revert action is focused.
- Used shared availability rules for action appearance and controller activation; kept recovery available with a valid backup and Exit available on game-check errors.
- Added this changelog, linked it from the README, and included it in documentation and source bundles.
- Added download links and the application icon to the repository README.

## 0.1.8

- Replaced button names in the footer with colored controller symbols and up/down arrows, retaining text action labels.
- Added the Circle exit hint and context-specific footer controls for confirmations and credits.
- Used the same symbols in confirmation prompts.

## 0.1.7

- Removed experimental wording from the app, README and unlock-data comments.
- Removed the experimental-skins status line and moved the general status message into the freed space.

## 0.1.6

- Renamed the application and XMB title from **R2TK Unlock Manager** to **Resistance 2 Tool Kit**; updated documentation, icon title and installer filename.
- Added exit confirmation for Circle and the Exit action: Cross exits; Circle stays.
- Kept Circle returning from credits and canceling other confirmations.
- Required a new Cross press to accept, gave simultaneous cancel/confirm priority to cancellation, and displayed confirmation even on game-check errors.

## 0.1.5

- Changed **The original game** to **The game** in the DAT unlock description.
- Added hold-to-scroll for Up/Down, with an initial delay and controlled repeats; selections and confirmations remain single-press actions.
- Reset navigation repeats on release, disconnection and modal screens.
- Made the application icon background and monogram cutout transparent; added its SVG source and optional PNG renderer.

## 0.1.4

- Clarified that Revert to Original restores the unlock state before the first **Apply Selection**, including pre-existing DLC.
- Clarified that opening the app alone does not create the backup.
- Documented the backup location and safe restore-point reset procedure.

## 0.1.3

- Removed manual backup and USB export options from the menu; retained automatic backup before the first apply.
- Simplified actions to **Apply Selection**, **Revert to Original**, and **Exit**.
- Updated confirmation routing, navigation and interrupted-operation recovery for the simplified menu.
- Clarified preservation of existing DLC and the limitations of DLC added after the first backup; map files and licenses remain outside the managed scope.

## 0.1.2

- Added a read-only runtime heuristic to show the RPCS3 header and patch-comparison reminder only when RPCS3 is detected.
- Kept detection independent of unlock and backup operations.
- Clarified automatic backup and missing-USB error handling in documentation.

## 0.1.1

- Replaced confusing **Backup Originals** wording with **Back Up Game Unlocks** and clarified restore labels, prompts and completion messages.
- Changed the toolkit-installed status label to **Installed**.
- Removed unconditional RPCS3 comparison advice from the console interface.

## 0.1.0

- Added an offline, controller-operated unlock manager for BCUS98120 update 1.60 on PS3 CFW.
- Added individual cosmetic unlock selections for Collector Wraith, Malikov, Grim, Rachael Parker, Female Soldier, Ravager, Cloven, Ranger Variation and Black Ops Variation.
- Added DAT/EDAT marker management without modifying game executables, saves or archives.
- Added immutable automatic backup before the first apply, manual backup, USB backup export and restore actions.
- Preserved existing unlock files, detected external conflicts, and added staged writes with interrupted-operation recovery.
- Added original-game checks, menu navigation, select/deselect all, Credits, **by Guidjlk** attribution, application icon, and installable PKG packaging.
- Included original-source licensing and third-party credits/notices.
