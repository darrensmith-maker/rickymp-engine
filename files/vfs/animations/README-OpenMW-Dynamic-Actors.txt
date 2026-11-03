
OpenMW DYNAMIC ACTORS
=====================

---------------------------------------
version 1.0	by taitechnic


This mod requires OpenMW version 0.49 or better

Plays idle animations on the player when stationery. Plays extra idle animations on NPC's when a Dialogue window is opened with them. Auto and manual camera control while Dialogue window is open. Choose a standalone animation selected from a playlist, and play it on the player anytime using a hotkey.


Installation
------------

Create a folder, extract the files into it, and register this folder as a Data Directory in the OpenMW launcher. Use the launcher to enable the "Dynamic Actors.omwscripts" file.


FEATURES
--------

Features currently supported :

	Autoswitch to First Person view when Dialogue window opens
	Auto zoom-in towards NPC when Dialogue window opens
	Enable manual camera control during Dialogue when in 3rd Person view

	Choose a new idle animation for the player, that plays when standing still
	Can have additional random animations that play on top of the above

	Auto remove helmet when not in combat stance

	Play custom standalone animation using Hotkey.

	Disable game pause when Dialogue window is opened
	Extra scripted random animations are played on NPC during Dialogue
	Make NPC turn to face you during Dialogue



Usage in Dialogue windows
-------------------------

The game will no longer be paused when using any Dialogue window. If you are in 1st Person view, the camera will zoom-in closer to the NPC you are talking to. The mod settings page allows you to set the distance the camera will be fixed at from the NPC, in 1st person auto zoom. You can also enable auto switching to 1st Person view whenever Dialogue is opened.


If in 3rd Person view, using the ACTION hotkey ('P' key by default) during dialogue will enable Camera Control mode. Forward/Back/Left/Right movement controls, and the mouse and mouse wheel, can be used to move and zoom the camera. Use ACTION hotkey again to turn off camera control.


The NPC you are talking to will be turned to face you while the Dialogue window is open. A random selection of scripted idle animations will also be played on them at various intervals. The auto-turning and scripted idles can be disabled in mod Settings.


The settings page also allows you to re-enable game pausing during Dialogue windows. Note that if you do this, all of the above Dialogue features will be disabled.



Usage for Player
----------------

When you switch out of Weapon/Spellcasting stance, any headgear will be removed, and the script will remember what the headgear was. When you switch back to a combat stance, the previously worn headgear will be re-equipped. NOTE: The headgear you use will not be remembered in your Save file.


After standing still for several seconds, various idle animations will start to play on the Player. These will continue to play even if you use menus or turn to change direction. If you use any other movement action, or change to a combat stance, the idle animations will be turned off.


Pressing the ACTION hotkey will switch the camera to PREVIEW mode, and play a looping standalone animation on the Player. Movement controls will be disabled, and Forward/Back/Left/Right controls will now move the camera instead of the player. Press the ACTION hotkey again to return to normal player controls.
If any kind of movement action is forced on the player, or if combat stance is switched to, normal player controls will automatically be re-enabled.

While in camera control mode, using the 'Jump' key will switch to animation selection mode, and the Left/Right controls will now allow you to scroll through a playlist of animations. Find the animation name you want, hit the Action hotkey to turn off the current animation, then hit it again to turn on the new animation you selected.

Most of the animation names are self explanatory as to what they do.
'GV WALL LEAN 180' will turn you 180 degrees, and lean you backwards.
'VA SITTING CHAIR 180' will move you forwards 55 units, turn you 180 degrees, then place you in a sitting pose.
The intended usage of these two animations is that you stand in front of a wall or a chair, activate the animation, and you should end up leaning against the wall or seated in the chair.

Currently, implementation for playing Playlist animations is basic. No checks are made for the environment, so there is nothing to stop you from clipping through objects or sitting on empty air. You will sometimes need careful positioning to make the effect look right. This feature is intended mostly for making screenshots and video clips, not productive gameplay.


The mod settings page allows you to customize or disable any of the above features. Choose which idle animation the player uses when stationery, and whether to add random animations as well. Disable auto headgear removal. Customize default ACTION hotkey for playing standalone animations, this is set to 'P' by default.



Conflicts with other Mods
-------------------------

Mods such as Animated Morrowind introduce new NPC's that have custom animations playing on them. Using the auto turning or random scripted idles feature on them produces visual glitches. By default, this mod will avoid using those features on any of these NPC's. The file 'scripts\DynamicActors\userConfig\Dialog NPC Blocklist.lua' contains a list of the record ID's that will be blocked in this way. If you want to apply the block to any NPC not already covered, you can edit this file to add your own entries.

A patch is also included with this install that disables the NPC auto turning feature of the mod UIMODES for this reason. Make sure this mod is loaded after UiModes in the game launcher load order.



Credits
-------

GrumblingVomit and Vidi Aquam for the original animation files that were modified for use in this mod.

