#include "windowmanagerimp.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>

#include <osgViewer/Viewer>

#include <MyGUI_UString.h>
#include <MyGUI_IPointer.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_PointerManager.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ClipboardManager.h>
#include <MyGUI_WidgetManager.h>
#include <MyGUI_TextBox.h>

// For BT_NO_PROFILE
#include <LinearMath/btQuickprof.h>

#include <SDL_keyboard.h>
#include <SDL_clipboard.h>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/GUIController.hpp"
/*
    End of tes3mp addition
*/

#include <components/debug/debuglog.hpp>

#include <components/sdlutil/sdlcursormanager.hpp>
#include <components/sdlutil/sdlvideowrapper.hpp>

#include <components/esm/esmreader.hpp>
#include <components/esm/esmwriter.hpp>

#include <components/fontloader/fontloader.hpp>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/imagemanager.hpp>

#include <components/sceneutil/workqueue.hpp>

#include <components/translation/translation.hpp>

#include <components/myguiplatform/myguiplatform.hpp>
#include <components/myguiplatform/myguirendermanager.hpp>
#include <components/myguiplatform/additivelayer.hpp>
#include <components/myguiplatform/scalinglayer.hpp>

#include <components/vfs/manager.hpp>

#include <components/widgets/tags.hpp>

#include <components/misc/resourcehelpers.hpp>
#include <components/misc/frameratelimiter.hpp>

#include "../mwbase/inputmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwrender/vismask.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "../mwrender/localmap.hpp"

#include "console.hpp"
#include "journalwindow.hpp"
#include "journalviewmodel.hpp"
#include "charactercreation.hpp"
#include "dialogue.hpp"
#include "statswindow.hpp"
#include "messagebox.hpp"
#include "tooltips.hpp"
#include "quickloot.hpp"
#include "scrollwindow.hpp"
#include "bookwindow.hpp"
#include "bookeditordialog.hpp"
#include "hud.hpp"
#include "mainmenu.hpp"
#include "countdialog.hpp"
#include "tradewindow.hpp"
#include "spellbuyingwindow.hpp"
#include "travelwindow.hpp"
#include "settingswindow.hpp"
#include "confirmationdialog.hpp"
#include "alchemywindow.hpp"
#include "spellwindow.hpp"
#include "quickkeysmenu.hpp"
#include "playeranimationmenu.hpp"
#include "loadingscreen.hpp"
#include "levelupdialog.hpp"
#include "waitdialog.hpp"
#include "enchantingdialog.hpp"
#include "trainingwindow.hpp"
#include "recharge.hpp"
#include "exposedwindow.hpp"
#include "cursor.hpp"
#include "merchantrepair.hpp"
#include "repair.hpp"
#include "soulgemdialog.hpp"
#include "companionwindow.hpp"
#include "inventorywindow.hpp"
#include "bookpage.hpp"
#include "itemview.hpp"
#include "videowidget.hpp"
#include "backgroundimage.hpp"
#include "itemwidget.hpp"
#include "screenfader.hpp"
#include "debugwindow.hpp"
#include "spellview.hpp"
#include "draganddrop.hpp"
#include "container.hpp"
#include "arenalocalization.hpp"
#include "controllers.hpp"
#include "jailscreen.hpp"
#include "itemchargeview.hpp"
#include "keyboardnavigation.hpp"
#include "resourceskin.hpp"

namespace MWGui
{
    WindowManager::WindowManager(
            SDL_Window* window, osgViewer::Viewer* viewer, osg::Group* guiRoot, Resource::ResourceSystem* resourceSystem, SceneUtil::WorkQueue* workQueue,
            const std::string& logpath, const std::string& resourcePath, bool consoleOnlyScripts, Translation::Storage& translationDataStorage,
            ToUTF8::FromType encoding, bool exportFonts, const std::string& versionDescription, const std::string& userDataPath)
      : mOldUpdateMask(0)
      , mOldCullMask(0)
      , mStore(nullptr)
      , mResourceSystem(resourceSystem)
      , mWorkQueue(workQueue)
      , mViewer(viewer)
      , mConsoleOnlyScripts(consoleOnlyScripts)
      , mCurrentModals()
      , mHud(nullptr)
      , mMap(nullptr)
      , mLocalMapRender(nullptr)
      , mToolTips(nullptr)
      , mQuickLoot(nullptr)
      , mStatsWindow(nullptr)
      , mMessageBoxManager(nullptr)
      , mConsole(nullptr)
      , mDialogueWindow(nullptr)
      , mDragAndDrop(nullptr)
      , mInventoryWindow(nullptr)
      , mScrollWindow(nullptr)
      , mBookWindow(nullptr)
      , mBookEditorDialog(nullptr)
      , mCountDialog(nullptr)
      , mTradeWindow(nullptr)
      , mSettingsWindow(nullptr)
      , mConfirmationDialog(nullptr)
      , mSpellWindow(nullptr)
      , mQuickKeysMenu(nullptr)
      , mPlayerAnimationMenu(nullptr)
      , mLoadingScreen(nullptr)
      , mWaitDialog(nullptr)
      , mSoulgemDialog(nullptr)
      , mVideoBackground(nullptr)
      , mVideoWidget(nullptr)
      , mWerewolfFader(nullptr)
      , mBlindnessFader(nullptr)
      , mHitFader(nullptr)
      , mScreenFader(nullptr)
      , mDebugWindow(nullptr)
      , mJailScreen(nullptr)
      , mContainerWindow(nullptr)
      , mCompanionWindow(nullptr)
      , mTranslationDataStorage (translationDataStorage)
      , mCharGen(nullptr)
      , mInputBlocker(nullptr)
      , mPhysicsGrabHint(nullptr)
      , mPhysicsGrabHintText(nullptr)
      , mCrosshairEnabled(Settings::Manager::getBool ("crosshair", "HUD"))
      , mSubtitlesEnabled(Settings::Manager::getBool ("subtitles", "GUI"))
      , mHitFaderEnabled(Settings::Manager::getBool ("hit fader", "GUI"))
      , mWerewolfOverlayEnabled(Settings::Manager::getBool ("werewolf overlay", "GUI"))
      , mHudEnabled(true)
      , mCursorVisible(true)
      , mCursorActive(true)
      , mPlayerBounty(-1)
      , mGui(nullptr)
      , mGuiModes()
      , mCursorManager(nullptr)
      , mGarbageDialogs()
      , mShown(GW_ALL)
      , mForceHidden(GW_None)
      , mAllowed(GW_ALL)
      , mRestAllowed(true)
      , mShowOwned(0)
      , mEncoding(encoding)
      , mVersionDescription(versionDescription)
      , mWindowVisible(true)
    {
        mScalingFactor = std::clamp(Settings::Manager::getFloat("scaling factor", "GUI"), 0.5f, 8.f);
        mGuiPlatform = new osgMyGUI::Platform(viewer, guiRoot, resourceSystem->getImageManager(), mScalingFactor);
        mGuiPlatform->initialise(resourcePath, (boost::filesystem::path(logpath) / "MyGUI.log").generic_string());

        mGui = new MyGUI::Gui;
        mGui->initialise("");

        createTextures();

        mArenaLocalization.reset(new ArenaLocalization(resourceSystem->getVFS(), encoding));
        MyGUI::LanguageManager::getInstance().eventRequestTag = MyGUI::newDelegate(this, &WindowManager::onRetrieveTag);

        // Load fonts
        mFontLoader.reset(new Gui::FontLoader(encoding, resourceSystem->getVFS(), userDataPath, mScalingFactor));
        mFontLoader->loadBitmapFonts(exportFonts);

        //Register own widgets with MyGUI
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Widgets::MWSkill>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Widgets::MWAttribute>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Widgets::MWSpell>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Widgets::MWEffectList>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Widgets::MWSpellEffect>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Widgets::MWDynamicStat>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Window>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<VideoWidget>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<BackgroundImage>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<osgMyGUI::AdditiveLayer>("Layer");
        MyGUI::FactoryManager::getInstance().registerFactory<osgMyGUI::ScalingLayer>("Layer");
        BookPage::registerMyGUIComponents ();
        ItemView::registerComponents();
        ItemChargeView::registerComponents();
        ItemWidget::registerComponents();
        SpellView::registerComponents();
        Gui::registerAllWidgets();

        MyGUI::FactoryManager::getInstance().registerFactory<MWGui::Controllers::ControllerFollowMouse>("Controller");

        MyGUI::FactoryManager::getInstance().registerFactory<ResourceImageSetPointerFix>("Resource", "ResourceImageSetPointer");
        MyGUI::FactoryManager::getInstance().registerFactory<AutoSizedResourceSkin>("Resource", "AutoSizedResourceSkin");
        MyGUI::ResourceManager::getInstance().load("core.xml");
        WindowManager::loadUserFonts();

        bool keyboardNav = Settings::Manager::getBool("keyboard navigation", "GUI");
        mKeyboardNavigation.reset(new KeyboardNavigation());
        mKeyboardNavigation->setEnabled(keyboardNav);
        Gui::ImageButton::setDefaultNeedKeyFocus(keyboardNav);

        mLoadingScreen = new LoadingScreen(mResourceSystem, mViewer);
        mWindows.push_back(mLoadingScreen);

        //set up the hardware cursor manager
        mCursorManager = new SDLUtil::SDLCursorManager();

        MyGUI::PointerManager::getInstance().eventChangeMousePointer += MyGUI::newDelegate(this, &WindowManager::onCursorChange);

        MyGUI::InputManager::getInstance().eventChangeKeyFocus += MyGUI::newDelegate(this, &WindowManager::onKeyFocusChanged);

        // Create all cursors in advance
        createCursors();
        onCursorChange(MyGUI::PointerManager::getInstance().getDefaultPointer());
        mCursorManager->setEnabled(true);

        // hide mygui's pointer
        MyGUI::PointerManager::getInstance().setVisible(false);

        mVideoBackground = MyGUI::Gui::getInstance().createWidgetReal<MyGUI::ImageBox>("ImageBox", 0,0,1,1,
            MyGUI::Align::Default, "InputBlocker");
        mVideoBackground->setImageTexture("black");
        mVideoBackground->setVisible(false);
        mVideoBackground->setNeedMouseFocus(true);
        mVideoBackground->setNeedKeyFocus(true);

        mVideoWidget = mVideoBackground->createWidgetReal<VideoWidget>("ImageBox", 0,0,1,1, MyGUI::Align::Default);
        mVideoWidget->setNeedMouseFocus(true);
        mVideoWidget->setNeedKeyFocus(true);
        mVideoWidget->setVFS(resourceSystem->getVFS());

        // Removes default MyGUI system clipboard implementation, which supports windows only
        MyGUI::ClipboardManager::getInstance().eventClipboardChanged.clear();
        MyGUI::ClipboardManager::getInstance().eventClipboardRequested.clear();

        MyGUI::ClipboardManager::getInstance().eventClipboardChanged += MyGUI::newDelegate(this, &WindowManager::onClipboardChanged);
        MyGUI::ClipboardManager::getInstance().eventClipboardRequested += MyGUI::newDelegate(this, &WindowManager::onClipboardRequested);

        mShowOwned = Settings::Manager::getInt("show owned", "Game");

        mVideoWrapper = new SDLUtil::VideoWrapper(window, viewer);
        mVideoWrapper->setGammaContrast(Settings::Manager::getFloat("gamma", "Video"),
                                        Settings::Manager::getFloat("contrast", "Video"));

        mStatsWatcher.reset(new StatsWatcher());
    }

    void WindowManager::loadUserFonts()
    {
        mFontLoader->loadTrueTypeFonts();
    }

    void WindowManager::initUI()
    {
        // Get size info from the Gui object
        int w = MyGUI::RenderManager::getInstance().getViewSize().width;
        int h = MyGUI::RenderManager::getInstance().getViewSize().height;

        mTextColours.loadColours();

        mDragAndDrop = new DragAndDrop();

        Recharge* recharge = new Recharge();
        mGuiModeStates[GM_Recharge] = GuiModeState(recharge);
        mWindows.push_back(recharge);

        MainMenu* menu = new MainMenu(w, h, mResourceSystem->getVFS(), mVersionDescription);
        mGuiModeStates[GM_MainMenu] = GuiModeState(menu);
        mWindows.push_back(menu);

        // ArenaMW workspace preset v1. Earlier cumulative patches temporarily enlarged
        // the statistics window to 40% x 60%, which made the four inventory-mode
        // windows overlap and also caused the stats document to be built against a
        // short initial viewport. Migrate once to the reference tiled workspace:
        //
        //   stats     | gameplay      | map
        //   (full H)  | inventory     | spells
        //
        // Afterwards normal window dragging keeps working and the user's coordinates
        // are persisted as usual; this migration will not run again.
        constexpr int ArenaWorkspaceLayoutVersion = 2;
        if (Settings::Manager::getInt("arena workspace layout version", "GUI") < ArenaWorkspaceLayoutVersion)
        {
            Settings::Manager::setFloat("stats x", "Windows", 0.0f);
            Settings::Manager::setFloat("stats y", "Windows", 0.0f);
            Settings::Manager::setFloat("stats w", "Windows", 0.2875f);
            Settings::Manager::setFloat("stats h", "Windows", 1.0f);

            Settings::Manager::setFloat("inventory x", "Windows", 0.2875f);
            Settings::Manager::setFloat("inventory y", "Windows", 0.5275f);
            Settings::Manager::setFloat("inventory w", "Windows", 0.452344f);
            Settings::Manager::setFloat("inventory h", "Windows", 0.4725f);

            Settings::Manager::setFloat("map x", "Windows", 0.739844f);
            Settings::Manager::setFloat("map y", "Windows", 0.0f);
            Settings::Manager::setFloat("map w", "Windows", 0.260156f);
            Settings::Manager::setFloat("map h", "Windows", 0.5275f);

            Settings::Manager::setFloat("spells x", "Windows", 0.739844f);
            Settings::Manager::setFloat("spells y", "Windows", 0.5275f);
            Settings::Manager::setFloat("spells w", "Windows", 0.260156f);
            Settings::Manager::setFloat("spells h", "Windows", 0.4725f);

            // Two-pane container/companion modes: player inventory on the left,
            // the active container/companion on the right. Barter has its own
            // independently versioned preset below.
            for (const std::string& prefix : { std::string("inventory container"),
                     std::string("inventory companion") })
            {
                Settings::Manager::setFloat(prefix + " x", "Windows", 0.08f);
                Settings::Manager::setFloat(prefix + " y", "Windows", 0.095f);
                Settings::Manager::setFloat(prefix + " w", "Windows", 0.415f);
                Settings::Manager::setFloat(prefix + " h", "Windows", 0.85f);
                Settings::Manager::setBool(prefix + " maximized", "Windows", false);
            }

            for (const std::string& prefix : { std::string("container"),
                     std::string("companion") })
            {
                Settings::Manager::setFloat(prefix + " x", "Windows", 0.503f);
                Settings::Manager::setFloat(prefix + " y", "Windows", 0.095f);
                Settings::Manager::setFloat(prefix + " w", "Windows", 0.442f);
                Settings::Manager::setFloat(prefix + " h", "Windows", 0.85f);
                Settings::Manager::setBool(prefix + " maximized", "Windows", false);
            }

            Settings::Manager::setInt("arena workspace layout version", "GUI", ArenaWorkspaceLayoutVersion);
        }

        // ArenaMW barter layout preset v1. Keep the two barter panes completely
        // independent, but give a fresh profile (and profiles made with older
        // cumulative patches) the compact side-by-side composition from the
        // reference 1360x768 screenshot. This migration intentionally touches
        // only barter geometry, so it does not reset the normal inventory/map/
        // stats/spells workspace positions.
        constexpr int ArenaBarterLayoutVersion = 1;
        if (Settings::Manager::getInt("arena barter layout version", "GUI") < ArenaBarterLayoutVersion)
        {
            Settings::Manager::setFloat("inventory barter x", "Windows", 0.050f);
            Settings::Manager::setFloat("inventory barter y", "Windows", 0.135f);
            Settings::Manager::setFloat("inventory barter w", "Windows", 0.393f);
            Settings::Manager::setFloat("inventory barter h", "Windows", 0.641f);
            Settings::Manager::setBool("inventory barter maximized", "Windows", false);

            Settings::Manager::setFloat("barter x", "Windows", 0.519f);
            Settings::Manager::setFloat("barter y", "Windows", 0.135f);
            Settings::Manager::setFloat("barter w", "Windows", 0.392f);
            Settings::Manager::setFloat("barter h", "Windows", 0.641f);
            Settings::Manager::setBool("barter maximized", "Windows", false);

            Settings::Manager::setInt("arena barter layout version", "GUI", ArenaBarterLayoutVersion);
        }

        mLocalMapRender = new MWRender::LocalMap(mViewer->getSceneData()->asGroup());
        mMap = new MapWindow(mCustomMarkers, mDragAndDrop, mLocalMapRender, mWorkQueue);
        mWindows.push_back(mMap);
        mMap->renderGlobalMap();
        trackWindow(mMap, "map");

        mStatsWindow = new StatsWindow(mDragAndDrop);
        mWindows.push_back(mStatsWindow);
        trackWindow(mStatsWindow, "stats");

        mInventoryWindow = new InventoryWindow(mDragAndDrop, mViewer->getSceneData()->asGroup(), mResourceSystem);
        mWindows.push_back(mInventoryWindow);

        mSpellWindow = new SpellWindow(mDragAndDrop);
        mWindows.push_back(mSpellWindow);
        trackWindow(mSpellWindow, "spells");

        mGuiModeStates[GM_Inventory] = GuiModeState({mMap, mInventoryWindow, mSpellWindow, mStatsWindow});
        mGuiModeStates[GM_None] = GuiModeState({mMap, mInventoryWindow, mSpellWindow, mStatsWindow});

        mTradeWindow = new TradeWindow(mDragAndDrop);
        mWindows.push_back(mTradeWindow);
        trackWindow(mTradeWindow, "barter");
        mGuiModeStates[GM_Barter] = GuiModeState({mInventoryWindow, mTradeWindow});

        mConsole = new Console(w,h, mConsoleOnlyScripts);
        mWindows.push_back(mConsole);
        trackWindow(mConsole, "console");

        bool questList = mResourceSystem->getVFS()->exists("textures/tx_menubook_options_over.dds");
        JournalWindow* journal = JournalWindow::create(JournalViewModel::create (), questList, mEncoding);
        mWindows.push_back(journal);
        mGuiModeStates[GM_Journal] = GuiModeState(journal);
        mGuiModeStates[GM_Journal].mCloseSound = "book close";
        mGuiModeStates[GM_Journal].mOpenSound = "book open";

        mMessageBoxManager = new MessageBoxManager(mStore->get<ESM::GameSetting>().find("fMessageTimePerChar")->mValue.getFloat());

        SpellBuyingWindow* spellBuyingWindow = new SpellBuyingWindow();
        mWindows.push_back(spellBuyingWindow);
        mGuiModeStates[GM_SpellBuying] = GuiModeState(spellBuyingWindow);

        TravelWindow* travelWindow = new TravelWindow();
        mWindows.push_back(travelWindow);
        mGuiModeStates[GM_Travel] = GuiModeState(travelWindow);

        mDialogueWindow = new DialogueWindow();
        mWindows.push_back(mDialogueWindow);
        trackWindow(mDialogueWindow, "dialogue");
        mGuiModeStates[GM_Dialogue] = GuiModeState(mDialogueWindow);
        mTradeWindow->eventTradeDone += MyGUI::newDelegate(mDialogueWindow, &DialogueWindow::onTradeComplete);

        /*
            Start of tes3mp change (major)

            Use a member variable (mContainerWIndow) instead of a local one so
            we can access it from elsewhere
        */
        mContainerWindow = new ContainerWindow(mDragAndDrop);
        mWindows.push_back(mContainerWindow);
        trackWindow(mContainerWindow, "container");
        mGuiModeStates[GM_Container] = GuiModeState({mContainerWindow, mInventoryWindow});
        /*
            End of tes3mp change (major)
        */

        mHud = new HUD(mCustomMarkers, mDragAndDrop, mLocalMapRender);
        mWindows.push_back(mHud);

        mToolTips = new ToolTips();
        mQuickLoot = new QuickLoot();

        mScrollWindow = new ScrollWindow();
        mWindows.push_back(mScrollWindow);
        mGuiModeStates[GM_Scroll] = GuiModeState(mScrollWindow);
        mGuiModeStates[GM_Scroll].mOpenSound = "scroll";
        mGuiModeStates[GM_Scroll].mCloseSound = "scroll";

        mBookWindow = new BookWindow();
        mWindows.push_back(mBookWindow);
        mGuiModeStates[GM_Book] = GuiModeState(mBookWindow);
        mGuiModeStates[GM_Book].mOpenSound = "book open";
        mGuiModeStates[GM_Book].mCloseSound = "book close";

        mBookEditorDialog = new BookEditorDialog();
        mWindows.push_back(mBookEditorDialog);

        mCountDialog = new CountDialog();
        mWindows.push_back(mCountDialog);

        mSettingsWindow = new SettingsWindow();
        mWindows.push_back(mSettingsWindow);
        mGuiModeStates[GM_Settings] = GuiModeState(mSettingsWindow);

        mConfirmationDialog = new ConfirmationDialog();
        mWindows.push_back(mConfirmationDialog);

        AlchemyWindow* alchemyWindow = new AlchemyWindow();
        mWindows.push_back(alchemyWindow);
        trackWindow(alchemyWindow, "alchemy");
        mGuiModeStates[GM_Alchemy] = GuiModeState(alchemyWindow);

        mQuickKeysMenu = new QuickKeysMenu();
        mWindows.push_back(mQuickKeysMenu);
        mGuiModeStates[GM_QuickKeysMenu] = GuiModeState(mQuickKeysMenu);

        mPlayerAnimationMenu = new PlayerAnimationMenu();
        mWindows.push_back(mPlayerAnimationMenu);
        mGuiModeStates[GM_PlayerAnimationMenu] = GuiModeState(mPlayerAnimationMenu);

        LevelupDialog* levelupDialog = new LevelupDialog();
        mWindows.push_back(levelupDialog);
        mGuiModeStates[GM_Levelup] = GuiModeState(levelupDialog);

        mWaitDialog = new WaitDialog();
        mWindows.push_back(mWaitDialog);
        mGuiModeStates[GM_Rest] = GuiModeState({mWaitDialog->getProgressBar(), mWaitDialog});

        SpellCreationDialog* spellCreationDialog = new SpellCreationDialog();
        mWindows.push_back(spellCreationDialog);
        mGuiModeStates[GM_SpellCreation] = GuiModeState(spellCreationDialog);

        EnchantingDialog* enchantingDialog = new EnchantingDialog();
        mWindows.push_back(enchantingDialog);
        mGuiModeStates[GM_Enchanting] = GuiModeState(enchantingDialog);

        TrainingWindow* trainingWindow = new TrainingWindow();
        mWindows.push_back(trainingWindow);
        mGuiModeStates[GM_Training] = GuiModeState({trainingWindow->getProgressBar(), trainingWindow});

        MerchantRepair* merchantRepair = new MerchantRepair();
        mWindows.push_back(merchantRepair);
        mGuiModeStates[GM_MerchantRepair] = GuiModeState(merchantRepair);

        Repair* repair = new Repair();
        mWindows.push_back(repair);
        mGuiModeStates[GM_Repair] = GuiModeState(repair);

        mSoulgemDialog = new SoulgemDialog(mMessageBoxManager);

        mCompanionWindow = new CompanionWindow(mDragAndDrop, mMessageBoxManager);
        mWindows.push_back(mCompanionWindow);
        trackWindow(mCompanionWindow, "companion");
        mGuiModeStates[GM_Companion] = GuiModeState({mInventoryWindow, mCompanionWindow});

        mJailScreen = new JailScreen();
        mWindows.push_back(mJailScreen);
        mGuiModeStates[GM_Jail] = GuiModeState(mJailScreen);

        std::string werewolfFaderTex = "textures\\werewolfoverlay.dds";
        if (mResourceSystem->getVFS()->exists(werewolfFaderTex))
        {
            mWerewolfFader = new ScreenFader(werewolfFaderTex);
            mWindows.push_back(mWerewolfFader);
        }
        mBlindnessFader = new ScreenFader("black");
        mWindows.push_back(mBlindnessFader);

        // fall back to player_hit_01.dds if bm_player_hit_01.dds is not available
        std::string hitFaderTexture = "textures\\bm_player_hit_01.dds";
        const std::string hitFaderLayout = "openmw_screen_fader_hit.layout";
        MyGUI::FloatCoord hitFaderCoord (0,0,1,1);
        if(!mResourceSystem->getVFS()->exists(hitFaderTexture))
        {
            hitFaderTexture = "textures\\player_hit_01.dds";
            hitFaderCoord = MyGUI::FloatCoord(0.2, 0.25, 0.6, 0.5);
        }
        mHitFader = new ScreenFader(hitFaderTexture, hitFaderLayout, hitFaderCoord);
        mWindows.push_back(mHitFader);

        mScreenFader = new ScreenFader("black");
        mWindows.push_back(mScreenFader);

        mDebugWindow = new DebugWindow();
        mWindows.push_back(mDebugWindow);

        mInputBlocker = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>("",0,0,w,h,MyGUI::Align::Stretch,"InputBlocker");

        // Persistent placement helper styled and positioned like the Z animation
        // menu: a compact BlackBG panel on the left side of the screen.  It stays
        // non-modal and never captures mouse/key focus while an object is held.
        mPhysicsGrabHint = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
            "BlackBG", 0, 0, 310, 166, MyGUI::Align::Default, "Popup");
        mPhysicsGrabHint->setNeedMouseFocus(false);
        mPhysicsGrabHint->setNeedKeyFocus(false);

        MyGUI::TextBox* physicsGrabHintTitle = mPhysicsGrabHint->createWidget<MyGUI::TextBox>(
            "SandBrightText", 10, 7, 290, 22, MyGUI::Align::Top | MyGUI::Align::HStretch, "PhysicsGrabHintTitle");
        physicsGrabHintTitle->setNeedMouseFocus(false);
        physicsGrabHintTitle->setNeedKeyFocus(false);
        physicsGrabHintTitle->setTextAlign(MyGUI::Align::Center);
        physicsGrabHintTitle->setCaption(MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=placement.title}"));

        mPhysicsGrabHintText = mPhysicsGrabHint->createWidget<MyGUI::TextBox>(
            "SandBrightText", 12, 34, 286, 124, MyGUI::Align::Stretch, "PhysicsGrabHintText");
        mPhysicsGrabHintText->setNeedMouseFocus(false);
        mPhysicsGrabHintText->setNeedKeyFocus(false);
        mPhysicsGrabHintText->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        mPhysicsGrabHint->setVisible(false);

        mHud->setVisible(true);

        mCharGen = new CharacterCreation(mViewer->getSceneData()->asGroup(), mResourceSystem);

        updatePinnedWindows();

        // Set up visibility
        updateVisible();

        mStatsWatcher->addListener(mHud);
        mStatsWatcher->addListener(mStatsWindow);
        mStatsWatcher->addListener(mCharGen);
    }

    int WindowManager::getFontHeight() const
    {
        return mFontLoader->getFontHeight();
    }

    void WindowManager::setNewGame(bool newgame)
    {
        if (newgame)
        {
            disallowAll();

            mStatsWatcher->removeListener(mCharGen);
            delete mCharGen;
            mCharGen = new CharacterCreation(mViewer->getSceneData()->asGroup(), mResourceSystem);
            mStatsWatcher->addListener(mCharGen);
        }
        else
            allow(GW_ALL);

        mStatsWatcher->forceUpdate();
    }

    WindowManager::~WindowManager()
    {
        try
        {
            mStatsWatcher.reset();

            MyGUI::LanguageManager::getInstance().eventRequestTag.clear();
            MyGUI::PointerManager::getInstance().eventChangeMousePointer.clear();
            MyGUI::InputManager::getInstance().eventChangeKeyFocus.clear();
            MyGUI::ClipboardManager::getInstance().eventClipboardChanged.clear();
            MyGUI::ClipboardManager::getInstance().eventClipboardRequested.clear();

            for (WindowBase* window : mWindows)
                delete window;
            mWindows.clear();

            delete mMessageBoxManager;
            delete mLocalMapRender;
            delete mCharGen;
            delete mDragAndDrop;
            delete mSoulgemDialog;
            delete mCursorManager;
            delete mToolTips;

            mKeyboardNavigation.reset();

            delete mQuickLoot;
            cleanupGarbage();

            mFontLoader.reset();

            mGui->shutdown();
            delete mGui;

            mGuiPlatform->shutdown();
            delete mGuiPlatform;
            delete mVideoWrapper;
        }
        catch(const MyGUI::Exception& e)
        {
            Log(Debug::Error) << "Error in the destructor: " << e.what();
        }
    }

    void WindowManager::setStore(const MWWorld::ESMStore &store)
    {
        mStore = &store;
    }

    void WindowManager::cleanupGarbage()
    {
        // Delete any dialogs which are no longer in use
        if (!mGarbageDialogs.empty())
        {
            for (Layout* widget : mGarbageDialogs)
            {
                delete widget;
            }
            mGarbageDialogs.clear();
        }
    }

    void WindowManager::enableScene(bool enable)
    {
        unsigned int disablemask = MWRender::Mask_GUI|MWRender::Mask_PreCompile;
        if (!enable && mViewer->getCamera()->getCullMask() != disablemask)
        {
            mOldUpdateMask = mViewer->getUpdateVisitor()->getTraversalMask();
            mOldCullMask = mViewer->getCamera()->getCullMask();
            mViewer->getUpdateVisitor()->setTraversalMask(disablemask);
            mViewer->getCamera()->setCullMask(disablemask);
        }
        else if (enable && mViewer->getCamera()->getCullMask() == disablemask)
        {
            mViewer->getUpdateVisitor()->setTraversalMask(mOldUpdateMask);
            mViewer->getCamera()->setCullMask(mOldCullMask);
        }
    }

    void WindowManager::updateConsoleObjectPtr(const MWWorld::Ptr& currentPtr, const MWWorld::Ptr& newPtr)
    {
        mConsole->updateSelectedObjectPtr(currentPtr, newPtr);
    }

    void WindowManager::updateVisible()
    {
        bool loading = (getMode() == GM_Loading || getMode() == GM_LoadingWallpaper);

        bool mainmenucover = containsMode(GM_MainMenu) && MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame;

        enableScene(!loading && !mainmenucover);

        if (!mMap)
            return; // UI not created yet

        mHud->setVisible(mHudEnabled && !loading);
        mToolTips->setVisible(mHudEnabled && !loading);

        bool gameMode = !isGuiMode();

        MWBase::Environment::get().getInputManager()->changeInputMode(!gameMode);

        mInputBlocker->setVisible (gameMode);

        if (loading)
            setCursorVisible(mMessageBoxManager && mMessageBoxManager->isInteractiveMessageBox());
        else
            setCursorVisible(!gameMode);

        if (gameMode)
            setKeyFocusWidget (nullptr);

        // Icons of forced hidden windows are displayed
        setMinimapVisibility((mAllowed & GW_Map) && (!mMap->pinned() || (mForceHidden & GW_Map)));
        setWeaponVisibility((mAllowed & GW_Inventory) && (!mInventoryWindow->pinned() || (mForceHidden & GW_Inventory)));
        setSpellVisibility((mAllowed & GW_Magic) && (!mSpellWindow->pinned() || (mForceHidden & GW_Magic)));
        setHMSVisibility((mAllowed & GW_Stats) && (!mStatsWindow->pinned() || (mForceHidden & GW_Stats)));

        mInventoryWindow->setGuiMode(getMode());

        // If in game mode (or interactive messagebox), show the pinned windows
        if (mGuiModes.empty())
        {
            mMap->setVisible(mMap->pinned() && !isConsoleMode() && !(mForceHidden & GW_Map) && (mAllowed & GW_Map));
            mStatsWindow->setVisible(mStatsWindow->pinned() && !isConsoleMode() && !(mForceHidden & GW_Stats) && (mAllowed & GW_Stats));
            mInventoryWindow->setVisible(mInventoryWindow->pinned() && !isConsoleMode() && !(mForceHidden & GW_Inventory) && (mAllowed & GW_Inventory));
            mSpellWindow->setVisible(mSpellWindow->pinned() && !isConsoleMode() && !(mForceHidden & GW_Magic) && (mAllowed & GW_Magic));
            return;
        }
        else if (getMode() != GM_Inventory)
        {
            mMap->setVisible(false);
            mStatsWindow->setVisible(false);
            mSpellWindow->setVisible(false);
            mInventoryWindow->setVisible(getMode() == GM_Container || getMode() == GM_Barter || getMode() == GM_Companion);
        }

        GuiMode mode = mGuiModes.back();

        mInventoryWindow->setTrading(mode == GM_Barter);

        if (getMode() == GM_Inventory)
        {
            // For the inventory mode, compute the effective set of windows to show.
            // This is controlled both by what windows the
            // user has opened/closed (the 'shown' variable) and by what
            // windows we are allowed to show (the 'allowed' var.)
            int eff = mShown & mAllowed & ~mForceHidden;
            mMap->setVisible(eff & GW_Map);
            mInventoryWindow->setVisible(eff & GW_Inventory);
            mSpellWindow->setVisible(eff & GW_Magic);
            mStatsWindow->setVisible(eff & GW_Stats);
        }

        switch (mode)
        {
        // FIXME: refactor chargen windows to use modes properly (or not use them at all)
        case GM_Name:
        case GM_Race:
        case GM_Class:
        case GM_ClassPick:
        case GM_ClassCreate:
        case GM_Birth:
        case GM_ClassGenerate:
        case GM_Review:
            mCharGen->spawnDialog(mode);
            break;
        default:
            /*
                Start of tes3mp addition

                Pass the GuiMode further on to the multiplayer-specific GUI controller
            */
            mwmp::Main::get().getGUIController()->WM_UpdateVisible(mode);
            /*
                End of tes3mp addition
            */
            break;
        }
    }

    void WindowManager::setDrowningTimeLeft (float time, float maxTime)
    {
        mHud->setDrowningTimeLeft(time, maxTime);
    }

    void WindowManager::removeDialog(Layout*dialog)
    {
        if (!dialog)
            return;
        dialog->setVisible(false);
        mGarbageDialogs.push_back(dialog);
    }

    void WindowManager::exitCurrentGuiMode()
    {
        if (mDragAndDrop && mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->finish();
            return;
        }

        GuiModeState& state = mGuiModeStates[mGuiModes.back()];
        for (WindowBase* window : state.mWindows)
        {
            if (!window->exit())
            {
                // unable to exit window, but give access to main menu
                if (!MyGUI::InputManager::getInstance().isModalAny() && getMode() != GM_MainMenu)
                    pushGuiMode (GM_MainMenu);
                return;
            }
        }

        popGuiMode();
    }

    /*
        Start of tes3mp change (major)

        Add a hasServerOrigin boolean to the list of arguments so those messageboxes
        can be differentiated from client-only ones

        Use the hasServerOrigin argument when creating an interactive message box
    */
    void WindowManager::interactiveMessageBox(const std::string &message, const std::vector<std::string> &buttons, bool block, bool hasServerOrigin)
    {
        mMessageBoxManager->createInteractiveMessageBox(message, buttons, hasServerOrigin);
    /*
        End of tes3mp change (major)
    */
        updateVisible();

        if (block)
        {
            Misc::FrameRateLimiter frameRateLimiter = Misc::makeFrameRateLimiter(MWBase::Environment::get().getFrameRateLimit());
            while (mMessageBoxManager->readPressedButton(false) == -1
                   && !MWBase::Environment::get().getStateManager()->hasQuitRequest())
            {
                const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(frameRateLimiter.getLastFrameDuration()).count();

                mKeyboardNavigation->onFrame();
                mMessageBoxManager->onFrame(dt);
                MWBase::Environment::get().getInputManager()->update(dt, true, false);

                if (!mWindowVisible)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                else
                {
                    mViewer->eventTraversal();
                    mViewer->updateTraversal();
                    mViewer->renderingTraversals();
                }
                // at the time this function is called we are in the middle of a frame,
                // so out of order calls are necessary to get a correct frameNumber for the next frame.
                // refer to the advance() and frame() order in Engine::go()
                mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());

                frameRateLimiter.limit();
            }
        }
    }

    void WindowManager::messageBox (const std::string& message, enum MWGui::ShowInDialogueMode showInDialogueMode)
    {
        // Barter is opened from dialogue, so the dialogue actor remains set while
        // the dialogue window is hidden behind the two barter panes. Ordinary
        // barter notifications must therefore bypass hidden dialogue history and
        // use the dedicated framed transient message box.
        if (containsMode(GM_Barter) && showInDialogueMode != MWGui::ShowInDialogueMode_Only)
        {
            mMessageBoxManager->createMessageBox(message);
            return;
        }

        // INFO result scripts can temporarily put another GUI mode above the
        // dialogue. Checking only getMode() loses quest, item and gold messages.
        // An assigned actor means the conversation is still active even during
        // a same-frame mode transition or an overlaid trade/persuasion window.
        const bool dialogueHasActor = mDialogueWindow && !mDialogueWindow->getPtr().isEmpty();
        const bool dialogueAvailable = mDialogueWindow
            && (dialogueHasActor || containsMode(GM_Dialogue) || mDialogueWindow->isVisible());

        if (dialogueAvailable && showInDialogueMode != MWGui::ShowInDialogueMode_Never)
        {
            const std::string text = MyGUI::LanguageManager::getInstance().replaceTags(message);
            if (!text.empty())
                mDialogueWindow->addMessageBox(text);
            return;
        }

        // Do not silently discard ShowInDialogueMode_Only notifications if a
        // script closes the dialogue immediately before creating the message.
        if (showInDialogueMode != MWGui::ShowInDialogueMode_Only || !message.empty())
            mMessageBoxManager->createMessageBox(message);
    }

    void WindowManager::staticMessageBox(const std::string& message)
    {
        mMessageBoxManager->createMessageBox(message, true);
    }

    void WindowManager::removeStaticMessageBox()
    {
        mMessageBoxManager->removeStaticMessageBox();
    }

    int WindowManager::readPressedButton ()
    {
        return mMessageBoxManager->readPressedButton();
    }

    std::string WindowManager::getGameSettingString(const std::string &id, const std::string &default_)
    {
        const ESM::GameSetting *setting = mStore->get<ESM::GameSetting>().search(id);

        if (setting && setting->mValue.getType()==ESM::VT_String)
            return setting->mValue.getString();

        return default_;
    }

    void WindowManager::updateMap()
    {
        if (!mLocalMapRender)
            return;

        MWWorld::ConstPtr player = MWMechanics::getPlayer();

        osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
        osg::Quat playerOrientation (-player.getRefData().getPosition().rot[2], osg::Vec3(0,0,1));

        osg::Vec3f playerdirection;
        int x,y;
        float u,v;
        mLocalMapRender->updatePlayer(playerPosition, playerOrientation, u, v, x, y, playerdirection);

        if (!player.getCell()->isExterior())
        {
            setActiveMap(x, y, true);
        }
        // else: need to know the current grid center, call setActiveMap from changeCell

        mMap->setPlayerDir(playerdirection.x(), playerdirection.y());
        mMap->setPlayerPos(x, y, u, v);
        mHud->setPlayerDir(playerdirection.x(), playerdirection.y());
        mHud->setPlayerPos(x, y, u, v);
    }

    void WindowManager::update (float frameDuration)
    {
        bool gameRunning = MWBase::Environment::get().getStateManager()->getState()!=
            MWBase::StateManager::State_NoGame;

        if (gameRunning)
            updateMap();

        if (!mGuiModes.empty())
        {
            GuiModeState& state = mGuiModeStates[mGuiModes.back()];
            for (WindowBase* window : state.mWindows)
                window->onFrame(frameDuration);
        }
        else
        {
            // update pinned windows if visible
            for (WindowBase* window : mGuiModeStates[GM_Inventory].mWindows)
                if (window->isVisible())
                    window->onFrame(frameDuration);
        }

        /*
            Start of tes3mp addition

            Fix crashes caused by messageboxes that never have their modals erased elsewhere, working around
            one of the main GUI-related problems that arise in an unpaused environment
        */
        for (auto modalIterator = mCurrentModals.begin(); modalIterator != mCurrentModals.end();) {
            if ((*modalIterator)->mMainWidget == 0)
            {
                mCurrentModals.erase(modalIterator);
            }
            else
            {
                ++modalIterator;
            }
        }
        /*
            End of tes3mp addition
        */

        // Make sure message boxes are always in front
        // This is an awful workaround for a series of awfully interwoven issues that couldn't be worked around
        // in a better way because of an impressive number of even more awfully interwoven issues.
        if (mMessageBoxManager && mMessageBoxManager->isInteractiveMessageBox() && mCurrentModals.back() != mMessageBoxManager->getInteractiveMessageBox())
        {
            std::vector<WindowModal*>::iterator found = std::find(mCurrentModals.begin(), mCurrentModals.end(), mMessageBoxManager->getInteractiveMessageBox());
            if (found != mCurrentModals.end())
            {
                WindowModal* msgbox = *found;
                std::swap(*found, mCurrentModals.back());
                MyGUI::InputManager::getInstance().addWidgetModal(msgbox->mMainWidget);
                mKeyboardNavigation->setModalWindow(msgbox->mMainWidget);
                mKeyboardNavigation->setDefaultFocus(msgbox->mMainWidget, msgbox->getDefaultKeyFocus());
            }
        }

        if (!mCurrentModals.empty())
            mCurrentModals.back()->onFrame(frameDuration);

        mKeyboardNavigation->onFrame();

        if (mMessageBoxManager)
            mMessageBoxManager->onFrame(frameDuration);

        mToolTips->onFrame(frameDuration);
        mQuickLoot->onFrame(frameDuration);

        if (mLocalMapRender)
            mLocalMapRender->cleanupCameras();

        if (!gameRunning)
            return;

        // We should display message about crime only once per frame, even if there are several crimes.
        // Otherwise we will get message spam when stealing several items via Take All button.
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        int currentBounty = player.getClass().getNpcStats(player).getBounty();
        if (currentBounty != mPlayerBounty)
        {
            if (mPlayerBounty >= 0 && currentBounty > mPlayerBounty)
                messageBox("#{sCrimeMessage}");

            mPlayerBounty = currentBounty;
        }

        mDragAndDrop->onFrame();

        mHud->onFrame(frameDuration);

        mDebugWindow->onFrame(frameDuration);

        if (mCharGen)
            mCharGen->onFrame(frameDuration);

        updateActivatedQuickKey();

        mStatsWatcher->update();

        cleanupGarbage();
    }

    void WindowManager::changeCell(const MWWorld::CellStore* cell)
    {
        mMap->requestMapRender(cell);

        std::string name = MWBase::Environment::get().getWorld()->getCellName (cell);

        mMap->setCellName( name );
        mHud->setCellName( name );

        if (cell->getCell()->isExterior())
        {
            if (!cell->getCell()->mName.empty())
                mMap->addVisitedLocation (name, cell->getCell()->getGridX (), cell->getCell()->getGridY ());

            mMap->cellExplored (cell->getCell()->getGridX(), cell->getCell()->getGridY());

            setActiveMap(cell->getCell()->getGridX(), cell->getCell()->getGridY(), false);
        }
        else
        {
            mMap->setCellPrefix (cell->getCell()->mName );
            mHud->setCellPrefix (cell->getCell()->mName );

            osg::Vec3f worldPos;
            if (!MWBase::Environment::get().getWorld()->findInteriorPositionInWorldSpace(cell, worldPos))
                worldPos = MWBase::Environment::get().getWorld()->getPlayer().getLastKnownExteriorPosition();
            else
                MWBase::Environment::get().getWorld()->getPlayer().setLastKnownExteriorPosition(worldPos);
            mMap->setGlobalMapPlayerPosition(worldPos.x(), worldPos.y());

            setActiveMap(0, 0, true);
        }
    }

    /*
        Start of tes3mp addition

        Allow the setting of the image data for a global map tile from elsewhere
        in the code
    */
    void WindowManager::setGlobalMapImage(int cellX, int cellY, const std::vector<char>& imageData)
    {
        mMap->setGlobalMapImage(cellX, cellY, imageData);
    }
    /*
        End of tes3mp addition
    */

    void WindowManager::setActiveMap(int x, int y, bool interior)
    {
        mMap->setActiveCell(x,y, interior);
        mHud->setActiveCell(x,y, interior);
    }

    void WindowManager::setDrowningBarVisibility(bool visible)
    {
        mHud->setDrowningBarVisible(visible);
    }

    void WindowManager::setHMSVisibility(bool visible)
    {
        mHud->setHmsVisible (visible);
    }

    void WindowManager::setMinimapVisibility(bool visible)
    {
        mHud->setMinimapVisible (visible);
    }

    bool WindowManager::toggleFogOfWar()
    {
        mMap->toggleFogOfWar();
        return mHud->toggleFogOfWar();
    }

    void WindowManager::setFocusObject(const MWWorld::Ptr& focus)
    {
        mToolTips->setFocusObject(focus);
        mQuickLoot->setFocusObject(focus);
        if (mHud)
            mHud->setFocusObject(focus);

        if(mHud && (mShowOwned == 2 || mShowOwned == 3))
        {
            bool owned = mToolTips->checkOwned();
            mHud->setCrosshairOwned(owned);
        }
    }

    void WindowManager::setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y)
    {
        mToolTips->setFocusObjectScreenCoords(min_x, min_y, max_x, max_y);
        mQuickLoot->setFocusObjectScreenCoords(min_x, min_y, max_x, max_y);
        if (mHud)
            mHud->setFocusObjectScreenCoords(min_x, min_y, max_x, max_y);
    }

    bool WindowManager::activateQuickLoot()
    {
        return mQuickLoot && mQuickLoot->activateSelected();
    }

    bool WindowManager::handlePlayerAnimationMenuMouseWheel(int rel)
    {
        return mPlayerAnimationMenu && getMode() == GM_PlayerAnimationMenu
            && mPlayerAnimationMenu->handleMouseWheel(rel);
    }

    bool WindowManager::handleQuickLootMouseWheel(int rel)
    {
        return mQuickLoot && mQuickLoot->handleMouseWheel(rel);
    }

    bool WindowManager::handleQuickLootKeyPress(MyGUI::KeyCode key)
    {
        return mQuickLoot && mQuickLoot->handleKeyPress(key);
    }

    bool WindowManager::isQuickLootVisible() const
    {
        return mQuickLoot && mQuickLoot->isVisible();
    }

    bool WindowManager::toggleFullHelp()
    {
        return mToolTips->toggleFullHelp();
    }

    bool WindowManager::getFullHelp() const
    {
        return mToolTips->getFullHelp();
    }

    void WindowManager::setWeaponVisibility(bool visible)
    {
        mHud->setWeapVisible (visible);
    }

    void WindowManager::setSpellVisibility(bool visible)
    {
        mHud->setSpellVisible (visible);
        mHud->setEffectVisible (visible);
    }

    void WindowManager::setSneakVisibility(bool visible)
    {
        mHud->setSneakVisible(visible);
    }

    void WindowManager::setDragDrop(bool dragDrop)
    {
        mToolTips->setEnabled(!dragDrop);
        MWBase::Environment::get().getInputManager()->setDragDrop(dragDrop);
    }

    /*
        Start of tes3mp addition

        Allow the completion of a drag and drop from elsewhere in the code
    */
    void WindowManager::finishDragDrop()
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
            mDragAndDrop->finish();
    }
    /*
        End of tes3mp addition
    */

    void WindowManager::setCursorVisible(bool visible)
    {
        mCursorVisible = visible;
    }

    void WindowManager::setCursorActive(bool active)
    {
        mCursorActive = active;
    }

    void WindowManager::onRetrieveTag(const MyGUI::UString& _tag, MyGUI::UString& _result)
    {
        std::string tag(_tag);
        
        std::string MyGuiPrefix = "setting=";
        size_t MyGuiPrefixLength = MyGuiPrefix.length();

        std::string tokenToFind = "sCell=";
        size_t tokenLength = tokenToFind.length();

        const std::string arenaPrefix = "arenamp=";
        if (tag.compare(0, arenaPrefix.length(), arenaPrefix) == 0)
        {
            _result = mArenaLocalization
                ? mArenaLocalization->translate(tag.substr(arenaPrefix.length()))
                : tag.substr(arenaPrefix.length());
        }
        else if(tag.compare(0, MyGuiPrefixLength, MyGuiPrefix) == 0)
        {
            tag = tag.substr(MyGuiPrefixLength, tag.length());
            size_t comma_pos = tag.find(',');
            std::string settingSection = tag.substr(0, comma_pos);
            std::string settingTag = tag.substr(comma_pos+1, tag.length());
            
            _result = Settings::Manager::getString(settingTag, settingSection);            
        }
        else if (tag.compare(0, tokenLength, tokenToFind) == 0)
        {
            _result = mTranslationDataStorage.translateCellName(tag.substr(tokenLength));
            _result = MyGUI::TextIterator::toTagsString(_result);
        }
        else if (Gui::replaceTag(tag, _result))
        {
            return;
        }
        else
        {
            if (!mStore)
            {
                Log(Debug::Error) << "Error: WindowManager::onRetrieveTag: no Store set up yet, can not replace '" << tag << "'";
                return;
            }
            const ESM::GameSetting *setting = mStore->get<ESM::GameSetting>().find(tag);

            if (setting && setting->mValue.getType()==ESM::VT_String)
                _result = setting->mValue.getString();
            else
                _result = tag;
        }
    }

    void WindowManager::processChangedSettings(const Settings::CategorySettingVector& changed)
    {
        mToolTips->setDelay(Settings::Manager::getFloat("tooltip delay", "GUI"));

        bool changeRes = false;
        for (const auto& setting : changed)
        {
            if (setting.first == "HUD" && setting.second == "crosshair")
                mCrosshairEnabled = Settings::Manager::getBool ("crosshair", "HUD");
            else if (setting.first == "GUI" && setting.second == "subtitles")
                mSubtitlesEnabled = Settings::Manager::getBool ("subtitles", "GUI");
            else if (setting.first == "GUI" && setting.second == "menu transparency")
                setMenuTransparency(Settings::Manager::getFloat("menu transparency", "GUI"));
            else if (setting.first == "GUI" && setting.second == "scaling factor")
                setScalingFactor(Settings::Manager::getFloat("scaling factor", "GUI"));
            else if (setting.first == "GUI" && (setting.second == "quick loot mode"
                    || setting.second == "quick loot"))
                mQuickLoot->setEnabled(Settings::Manager::getString("quick loot mode", "GUI") != "disabled");
            else if (setting.first == "GUI" && setting.second == "quick loot stationary delay")
                mQuickLoot->setStationaryDelay(
                    Settings::Manager::getFloat("quick loot stationary delay", "GUI"));
            else if (setting.first == "Video" && (
                    setting.second == "resolution x"
                    || setting.second == "resolution y"
                    || setting.second == "fullscreen"
                    || setting.second == "window border"))
                changeRes = true;

            else if (setting.first == "Video" && setting.second == "vsync")
                mVideoWrapper->setSyncToVBlank(Settings::Manager::getBool("vsync", "Video"));
            else if (setting.first == "Video" && (setting.second == "gamma" || setting.second == "contrast"))
                mVideoWrapper->setGammaContrast(Settings::Manager::getFloat("gamma", "Video"),
                                                Settings::Manager::getFloat("contrast", "Video"));
        }

        if (changeRes)
        {
            mVideoWrapper->setVideoMode(Settings::Manager::getInt("resolution x", "Video"),
                                        Settings::Manager::getInt("resolution y", "Video"),
                                        Settings::Manager::getBool("fullscreen", "Video"),
                                        Settings::Manager::getBool("window border", "Video"));
        }
    }

    void WindowManager::windowResized(int x, int y)
    {
        // SDL can emit startup resize events using a temporary/logical window
        // size (especially with Windows DPI scaling). Do not write that runtime
        // size back to settings.cfg: it would silently replace the resolution
        // selected in the launcher. Explicit resolution changes still go
        // through SettingsWindow and remain persistent.
        mGuiPlatform->getRenderManagerPtr()->setViewSize(x, y);

        // scaled size
        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        x = viewSize.width;
        y = viewSize.height;

        sizeVideo(x, y);

        if (!mHud)
            return; // UI not initialized yet

        for (std::map<MyGUI::Window*, std::string>::iterator it = mTrackedWindows.begin(); it != mTrackedWindows.end(); ++it)
        {
            std::string settingName = it->second;
            if (Settings::Manager::getBool(settingName + " maximized", "Windows"))
                settingName += " maximized";

            MyGUI::IntPoint pos(static_cast<int>(Settings::Manager::getFloat(settingName + " x", "Windows") * x),
                                static_cast<int>(Settings::Manager::getFloat(settingName + " y", "Windows") * y));
            MyGUI::IntSize size(static_cast<int>(Settings::Manager::getFloat(settingName + " w", "Windows") * x),
                                 static_cast<int>(Settings::Manager::getFloat(settingName + " h", "Windows") * y));
            it->first->setPosition(pos);
            it->first->setSize(size);
        }

        for (WindowBase* window : mWindows)
            window->onResChange(x, y);

        // We should reload TrueType fonts to fit new resolution
        loadUserFonts();

        // TODO: check if any windows are now off-screen and move them back if so
    }

    bool WindowManager::isWindowVisible()
    {
        return mWindowVisible;
    }

    std::string WindowManager::getArenaLanguage() const
    {
        return mArenaLocalization ? mArenaLocalization->getLanguage() : "en";
    }

    void WindowManager::setArenaLanguage(const std::string& language)
    {
        if (mArenaLocalization)
            mArenaLocalization->setLanguage(language);
    }

    void WindowManager::openBookWriter()
    {
        if (MWMechanics::isPlayerInCombat())
        {
            messageBox("#{arenamp=writer.no_combat}");
            return;
        }
        if (getMode() == GM_None)
            pushGuiMode(GM_Inventory);
        if (mBookEditorDialog)
            mBookEditorDialog->openNew();
    }

    void WindowManager::openBookWriter(const MWWorld::Ptr& source)
    {
        if (MWMechanics::isPlayerInCombat())
        {
            messageBox("#{arenamp=writer.no_combat}");
            return;
        }
        if (mBookEditorDialog)
            mBookEditorDialog->openForBook(source);
    }

    void WindowManager::windowVisibilityChange(bool visible)
    {
        mWindowVisible = visible;
    }

    void WindowManager::windowClosed()
    {
        MWBase::Environment::get().getStateManager()->requestQuit();
    }

    void WindowManager::onCursorChange(const std::string &name)
    {
        mCursorManager->cursorChanged(name);
    }

    void WindowManager::pushGuiMode(GuiMode mode)
    {
        pushGuiMode(mode, MWWorld::Ptr());
    }

    void WindowManager::pushGuiMode(GuiMode mode, const MWWorld::Ptr& arg)
    {
        if (mode==GM_Inventory && mAllowed==GW_None)
            return;

        if (mGuiModes.empty() || mGuiModes.back() != mode)
        {
            // If this mode already exists somewhere in the stack, just bring it to the front.
            if (std::find(mGuiModes.begin(), mGuiModes.end(), mode) != mGuiModes.end())
            {
                mGuiModes.erase(std::find(mGuiModes.begin(), mGuiModes.end(), mode));
            }

            if (!mGuiModes.empty())
            {
                mKeyboardNavigation->saveFocus(mGuiModes.back());
                mGuiModeStates[mGuiModes.back()].update(false);
            }
            mGuiModes.push_back(mode);

            mGuiModeStates[mode].update(true);
            playSound(mGuiModeStates[mode].mOpenSound);
        }
        for (WindowBase* window : mGuiModeStates[mode].mWindows)
            window->setPtr(arg);

        mKeyboardNavigation->restoreFocus(mode);

        updateVisible();
    }

    void WindowManager::popGuiMode(bool noSound)
    {
        if (mDragAndDrop && mDragAndDrop->mIsOnDragAndDrop)
        {
            mDragAndDrop->finish();
        }

        if (!mGuiModes.empty())
        {
            const GuiMode mode = mGuiModes.back();
            mKeyboardNavigation->saveFocus(mode);
            mGuiModes.pop_back();
            mGuiModeStates[mode].update(false);
            if (!noSound)
                playSound(mGuiModeStates[mode].mCloseSound);
        }

        if (!mGuiModes.empty())
        {
            const GuiMode mode = mGuiModes.back();
            mGuiModeStates[mode].update(true);
            mKeyboardNavigation->restoreFocus(mode);
        }

        updateVisible();

        // To make sure that console window get focus again
        if (mConsole && mConsole->isVisible())
            mConsole->onOpen();
    }

    void WindowManager::removeGuiMode(GuiMode mode, bool noSound)
    {
        if (!mGuiModes.empty() && mGuiModes.back() == mode)
        {
            popGuiMode(noSound);
            return;
        }

        std::vector<GuiMode>::iterator it = mGuiModes.begin();
        while (it != mGuiModes.end())
        {
            if (*it == mode)
                it = mGuiModes.erase(it);
            else
                ++it;
        }

        updateVisible();
    }

    void WindowManager::goToJail(int days)
    {
        pushGuiMode(MWGui::GM_Jail);
        mJailScreen->goToJail(days);
    }

    void WindowManager::setSelectedSpell(const std::string& spellId, int successChancePercent)
    {
        mSelectedSpell = spellId;
        mSelectedEnchantItem = MWWorld::Ptr();
        mHud->setSelectedSpell(spellId, successChancePercent);

        const ESM::Spell* spell = mStore->get<ESM::Spell>().find(spellId);

        mSpellWindow->setTitle(spell->mName);
    }

    void WindowManager::setSelectedEnchantItem(const MWWorld::Ptr& item)
    {
        mSelectedEnchantItem = item;
        mSelectedSpell = "";
        const ESM::Enchantment* ench = mStore->get<ESM::Enchantment>()
                .find(item.getClass().getEnchantment(item));

        int chargePercent = static_cast<int>(item.getCellRef().getNormalizedEnchantmentCharge(ench->mData.mCharge) * 100);
        mHud->setSelectedEnchantItem(item, chargePercent);
        mSpellWindow->setTitle(item.getClass().getName(item));
    }

    const MWWorld::Ptr &WindowManager::getSelectedEnchantItem() const
    {
        return mSelectedEnchantItem;
    }

    void WindowManager::setSelectedWeapon(const MWWorld::Ptr& item)
    {
        mSelectedWeapon = item;
        int durabilityPercent = 100;
        if (item.getClass().hasItemHealth(item))
        {
            durabilityPercent = static_cast<int>(item.getClass().getItemNormalizedHealth(item) * 100);
        }
        mHud->setSelectedWeapon(item, durabilityPercent);

        // The inventory window always represents the player.  The selected weapon
        // belongs on the HUD and must not replace the player name in either the
        // normal inventory or the player-side barter window.
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        const std::string playerName = player.getClass().getName(player);
        if (!playerName.empty())
            mInventoryWindow->setTitle(playerName);
    }

    const MWWorld::Ptr &WindowManager::getSelectedWeapon() const
    {
        return mSelectedWeapon;
    }

    void WindowManager::unsetSelectedSpell()
    {
        mSelectedSpell = "";
        mSelectedEnchantItem = MWWorld::Ptr();
        mHud->unsetSelectedSpell();

        MWWorld::Player* player = &MWBase::Environment::get().getWorld()->getPlayer();
        if (player->getDrawState() == MWMechanics::DrawState_Spell)
            player->setDrawState(MWMechanics::DrawState_Nothing);

        mSpellWindow->setTitle("#{sNone}");
    }

    void WindowManager::unsetSelectedWeapon()
    {
        mSelectedWeapon = MWWorld::Ptr();
        mHud->unsetSelectedWeapon();

        // Do not turn the inventory caption into "Hand-to-hand" when the weapon
        // is sheathed.  It is the player's inventory, so keep the player name.
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        const std::string playerName = player.getClass().getName(player);
        if (!playerName.empty())
            mInventoryWindow->setTitle(playerName);
    }

    void WindowManager::getMousePosition(int &x, int &y)
    {
        const MyGUI::IntPoint& pos = MyGUI::InputManager::getInstance().getMousePosition();
        x = pos.left;
        y = pos.top;
    }

    void WindowManager::getMousePosition(float &x, float &y)
    {
        const MyGUI::IntPoint& pos = MyGUI::InputManager::getInstance().getMousePosition();
        x = static_cast<float>(pos.left);
        y = static_cast<float>(pos.top);
        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        x /= viewSize.width;
        y /= viewSize.height;
    }

    bool WindowManager::getWorldMouseOver()
    {
        return mHud->getWorldMouseOver();
    }

    float WindowManager::getScalingFactor()
    {
        return mScalingFactor;
    }

    void WindowManager::setScalingFactor(float factor)
    {
        factor = std::clamp(factor, 0.5f, 3.0f);
        if (std::abs(factor - mScalingFactor) < 0.001f)
            return;
        mScalingFactor = factor;
        mGuiPlatform->getRenderManagerPtr()->setScalingFactor(factor);
        const int width = Settings::Manager::getInt("resolution x", "Video");
        const int height = Settings::Manager::getInt("resolution y", "Video");
        windowResized(width, height);
    }

    void WindowManager::executeInConsole (const std::string& path)
    {
        mConsole->executeFile (path);
    }

    /*
        Start of tes3mp addition

        Allow the execution of console commands from elsewhere in the code
    */
    void WindowManager::executeCommandInConsole(const std::string& command)
    {
        mConsole->execute(command);
    }
    /*
        End of tes3mp addition
    */

    MWGui::InventoryWindow* WindowManager::getInventoryWindow() { return mInventoryWindow; }
    MWGui::CountDialog* WindowManager::getCountDialog() { return mCountDialog; }
    MWGui::ConfirmationDialog* WindowManager::getConfirmationDialog() { return mConfirmationDialog; }
    MWGui::TradeWindow* WindowManager::getTradeWindow() { return mTradeWindow; }

    /*
        Start of tes3mp addition

        Make it possible to get the ContainerWindow from elsewhere
        in the code
    */
    MWGui::ContainerWindow* WindowManager::getContainerWindow() { return mContainerWindow; }

    MWGui::CompanionWindow* WindowManager::getCompanionWindow() { return mCompanionWindow; }
    /*
        End of tes3mp addition
    */

    /*
        Start of tes3mp addition

        Make it possible to get the DialogueWindow from elsewhere
    */
    MWGui::DialogueWindow* WindowManager::getDialogueWindow() { return mDialogueWindow; }
    /*
        End of tes3mp addition
    */

    void WindowManager::useItem(const MWWorld::Ptr &item, bool bypassBeastRestrictions)
    {
        if (mInventoryWindow)
            mInventoryWindow->useItem(item, bypassBeastRestrictions);
    }

    bool WindowManager::isAllowed (GuiWindow wnd) const
    {
        return (mAllowed & wnd) != 0;
    }

    void WindowManager::allow (GuiWindow wnd)
    {
        mAllowed = (GuiWindow)(mAllowed | wnd);

        if (wnd & GW_Inventory)
        {
            mBookWindow->setInventoryAllowed (true);
            mScrollWindow->setInventoryAllowed (true);
        }

        updateVisible();
    }

    void WindowManager::disallowAll()
    {
        mAllowed = GW_None;
        mRestAllowed = false;

        mBookWindow->setInventoryAllowed (false);
        mScrollWindow->setInventoryAllowed (false);

        updateVisible();
    }

    void WindowManager::toggleVisible (GuiWindow wnd)
    {
        if (getMode() != GM_Inventory)
            return;

        std::string settingName;
        switch (wnd)
        {
            case GW_Inventory:
                settingName = "inventory";
                break;
            case GW_Map:
                settingName = "map";
                break;
            case GW_Magic:
                settingName = "spells";
                break;
            case GW_Stats:
                settingName = "stats";
                break;
            default:
                break;
        }

        if (!settingName.empty())
        {
            settingName += " hidden";
            bool hidden = Settings::Manager::getBool(settingName, "Windows");
            Settings::Manager::setBool(settingName, "Windows", !hidden);
        }

        mShown = (GuiWindow)(mShown ^ wnd);
        updateVisible();
    }

    void WindowManager::forceHide(GuiWindow wnd)
    {
        mForceHidden = (GuiWindow)(mForceHidden | wnd);
        updateVisible();
    }

    void WindowManager::unsetForceHide(GuiWindow wnd)
    {
        mForceHidden = (GuiWindow)(mForceHidden & ~wnd);
        updateVisible();
    }

    bool WindowManager::isGuiMode() const
    {
        return
            !mGuiModes.empty() ||
            isConsoleMode() ||
            (mMessageBoxManager && mMessageBoxManager->isInteractiveMessageBox());
    }

    bool WindowManager::isConsoleMode() const
    {
        return mConsole && mConsole->isVisible();
    }

    MWGui::GuiMode WindowManager::getMode() const
    {
        if (mGuiModes.empty())
            return GM_None;
        return mGuiModes.back();
    }

    void WindowManager::disallowMouse()
    {
        mInputBlocker->setVisible (true);
    }

    void WindowManager::allowMouse()
    {
        mInputBlocker->setVisible (!isGuiMode ());
    }

    void WindowManager::notifyInputActionBound ()
    {
        mSettingsWindow->updateControlsBox ();
        allowMouse();
    }

    bool WindowManager::containsMode(GuiMode mode) const
    {
        if(mGuiModes.empty())
            return false;

        return std::find(mGuiModes.begin(), mGuiModes.end(), mode) != mGuiModes.end();
    }

    void WindowManager::showCrosshair (bool show)
    {
        if (mHud)
            mHud->setCrosshairVisible (show && mCrosshairEnabled);
    }

    void WindowManager::updateActivatedQuickKey ()
    {
        mQuickKeysMenu->updateActivatedQuickKey();
    }

    void WindowManager::activateQuickKey (int index)
    {
        mQuickKeysMenu->activateQuickKey(index);
    }

    /*
        Start of tes3mp addition

        Make it possible to add quickKeys from elsewhere in the code
    */
    void WindowManager::setQuickKey(int slot, int quickKeyType, MWWorld::Ptr item, const std::string& spellId)
    {
        if (slot > 0)
        {
            // The actual indexes recorded for quick keys are always 1 higher than their
            // indexes in the mKey vector, so adjust for the latter
            mQuickKeysMenu->setSelectedIndex(slot - 1);

            switch (quickKeyType)
            {
            case QuickKeysMenu::Type_Unassigned:
                mQuickKeysMenu->unassignIndex(slot - 1);
                break;
            case QuickKeysMenu::Type_Item:
                mQuickKeysMenu->onAssignItem(item);
                break;
            case QuickKeysMenu::Type_MagicItem:
                mQuickKeysMenu->onAssignMagicItem(item);
                break;
            case QuickKeysMenu::Type_Magic:
                mQuickKeysMenu->onAssignMagic(spellId);
                break;
            }
        }
    }
    /*
        End of tes3mp addition
    */

    bool WindowManager::getSubtitlesEnabled ()
    {
        return mSubtitlesEnabled;
    }

    bool WindowManager::toggleHud()
    {
        mHudEnabled = !mHudEnabled;
        updateVisible();
        return mHudEnabled;
    }

    bool WindowManager::getRestEnabled()
    {
        //Enable rest dialogue if character creation finished
        if(mRestAllowed==false && MWBase::Environment::get().getWorld()->getGlobalFloat ("chargenstate")==-1)
            mRestAllowed=true;
        return mRestAllowed;
    }

    bool WindowManager::getPlayerSleeping ()
    {
        return mWaitDialog->getSleeping();
    }

    void WindowManager::wakeUpPlayer()
    {
        mWaitDialog->wakeUp();
    }

    void WindowManager::addVisitedLocation(const std::string& name, int x, int y)
    {
        mMap->addVisitedLocation (name, x, y);
    }

    const Translation::Storage& WindowManager::getTranslationDataStorage() const
    {
        return mTranslationDataStorage;
    }

    void WindowManager::changePointer(const std::string &name)
    {
        MyGUI::PointerManager::getInstance().setPointer(name);
        onCursorChange(name);
    }

    void WindowManager::showSoulgemDialog(MWWorld::Ptr item)
    {
        mSoulgemDialog->show(item);
        updateVisible();
    }

    void WindowManager::updatePlayer()
    {
        mInventoryWindow->updatePlayer();

        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            setWerewolfOverlay(true);
            forceHide((GuiWindow)(MWGui::GW_Inventory | MWGui::GW_Magic));
        }
    }

    // Remove this wrapper once onKeyFocusChanged call is rendered unnecessary
    void WindowManager::setKeyFocusWidget(MyGUI::Widget *widget)
    {
        MyGUI::InputManager::getInstance().setKeyFocusWidget(widget);
        onKeyFocusChanged(widget);
    }

    void WindowManager::onKeyFocusChanged(MyGUI::Widget *widget)
    {
        if (widget && widget->castType<MyGUI::EditBox>(false))
            SDL_StartTextInput();
        else
            SDL_StopTextInput();
    }

    void WindowManager::setEnemy(const MWWorld::Ptr &enemy)
    {
        mHud->setEnemy(enemy);
    }

    int WindowManager::getMessagesCount() const
    {
        int count = 0;
        if (mMessageBoxManager)
            count = mMessageBoxManager->getMessagesCount();

        return count;
    }

    Loading::Listener* WindowManager::getLoadingScreen()
    {
        return mLoadingScreen;
    }

    bool WindowManager::getCursorVisible()
    {
        return mCursorVisible && mCursorActive;
    }

    void WindowManager::trackWindow(Layout *layout, const std::string &name)
    {
        std::string settingName = name;
        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        bool isMaximized = Settings::Manager::getBool(name + " maximized", "Windows");
        if (isMaximized)
            settingName += " maximized";

        MyGUI::IntPoint pos(static_cast<int>(Settings::Manager::getFloat(settingName + " x", "Windows") * viewSize.width),
                            static_cast<int>(Settings::Manager::getFloat(settingName + " y", "Windows") * viewSize.height));
        MyGUI::IntSize size (static_cast<int>(Settings::Manager::getFloat(settingName + " w", "Windows") * viewSize.width),
                             static_cast<int>(Settings::Manager::getFloat(settingName + " h", "Windows") * viewSize.height));

        // Keep tracked windows on-screen, but do not force the stats window back to
        // the temporary 40% x 60% size used by older cumulative patches. Workspace
        // preset v1 deliberately uses a narrower, full-height statistics column.
        if (name == "stats" && !isMaximized)
        {
            size.width = std::max(size.width, 280);
            size.height = std::max(size.height, 180);
            pos.left = std::max(0, std::min(pos.left, std::max(0, viewSize.width - size.width)));
            pos.top = std::max(0, std::min(pos.top, std::max(0, viewSize.height - size.height)));
        }

        layout->mMainWidget->setPosition(pos);
        layout->mMainWidget->setSize(size);

        MyGUI::Window* window = layout->mMainWidget->castType<MyGUI::Window>();
        window->eventWindowChangeCoord += MyGUI::newDelegate(this, &WindowManager::onWindowChangeCoord);
        mTrackedWindows[window] = name;
    }

    void WindowManager::toggleMaximized(Layout *layout)
    {
        MyGUI::Window* window = layout->mMainWidget->castType<MyGUI::Window>();
        std::string setting = mTrackedWindows[window];
        if (setting.empty())
            return;

        bool maximized = !Settings::Manager::getBool(setting + " maximized", "Windows");
        if (maximized)
            setting += " maximized";

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        float x = Settings::Manager::getFloat(setting + " x", "Windows") * float(viewSize.width);
        float y = Settings::Manager::getFloat(setting + " y", "Windows") * float(viewSize.height);
        float w = Settings::Manager::getFloat(setting + " w", "Windows") * float(viewSize.width);
        float h = Settings::Manager::getFloat(setting + " h", "Windows") * float(viewSize.height);
        window->setCoord(x, y, w, h);
        Settings::Manager::setBool(mTrackedWindows[window] + " maximized", "Windows", maximized);
    }

    void WindowManager::onWindowChangeCoord(MyGUI::Window *_sender)
    {
        std::string setting = mTrackedWindows[_sender];
        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        float x = _sender->getPosition().left / float(viewSize.width);
        float y = _sender->getPosition().top / float(viewSize.height);
        float w = _sender->getSize().width / float(viewSize.width);
        float h = _sender->getSize().height / float(viewSize.height);
        Settings::Manager::setFloat(setting + " x", "Windows", x);
        Settings::Manager::setFloat(setting + " y", "Windows", y);
        Settings::Manager::setFloat(setting + " w", "Windows", w);
        Settings::Manager::setFloat(setting + " h", "Windows", h);
        bool maximized = Settings::Manager::getBool(setting + " maximized", "Windows");
        if (maximized)
            Settings::Manager::setBool(setting + " maximized", "Windows", false);
    }

    void WindowManager::clear()
    {
        mPlayerBounty = -1;

        for (WindowBase* window : mWindows)
            window->clear();

        if (mLocalMapRender)
            mLocalMapRender->clear();

        mMessageBoxManager->clear();

        mToolTips->clear();
        mQuickLoot->clear();

        mSelectedSpell.clear();
        mCustomMarkers.clear();

        mForceHidden = GW_None;
        mRestAllowed = true;

        while (!mGuiModes.empty())
            popGuiMode();

        updateVisible();
    }

    void WindowManager::write(ESM::ESMWriter &writer, Loading::Listener& progress)
    {
        mMap->write(writer, progress);

        mQuickKeysMenu->write(writer);

        if (!mSelectedSpell.empty())
        {
            writer.startRecord(ESM::REC_ASPL);
            writer.writeHNString("ID__", mSelectedSpell);
            writer.endRecord(ESM::REC_ASPL);
        }

        for (CustomMarkerCollection::ContainerType::const_iterator it = mCustomMarkers.begin(); it != mCustomMarkers.end(); ++it)
        {
            writer.startRecord(ESM::REC_MARK);
            it->second.save(writer);
            writer.endRecord(ESM::REC_MARK);
        }
    }

    void WindowManager::readRecord(ESM::ESMReader &reader, uint32_t type)
    {
        if (type == ESM::REC_GMAP)
            mMap->readRecord(reader, type);
        else if (type == ESM::REC_KEYS)
            mQuickKeysMenu->readRecord(reader, type);
        else if (type == ESM::REC_ASPL)
        {
            reader.getSubNameIs("ID__");
            std::string spell = reader.getHString();
            if (mStore->get<ESM::Spell>().search(spell))
                mSelectedSpell = spell;
        }
        else if (type == ESM::REC_MARK)
        {
            ESM::CustomMarker marker;
            marker.load(reader);
            mCustomMarkers.addMarker(marker, false);
        }
    }

    int WindowManager::countSavedGameRecords() const
    {
        return 1 // Global map
                + 1 // QuickKeysMenu
                + mCustomMarkers.size()
                + (!mSelectedSpell.empty() ? 1 : 0);
    }

    bool WindowManager::isSavingAllowed() const
    {
        return !MyGUI::InputManager::getInstance().isModalAny()
                && !isConsoleMode()
                // TODO: remove this, once we have properly serialized the state of open windows
                && (!isGuiMode() || (mGuiModes.size() == 1 && (getMode() == GM_MainMenu || getMode() == GM_Rest)));
    }

    void WindowManager::playVideo(const std::string &name, bool allowSkipping)
    {
        mVideoWidget->playVideo("video\\" + name);

        mVideoWidget->eventKeyButtonPressed.clear();
        mVideoBackground->eventKeyButtonPressed.clear();
        if (allowSkipping)
        {
            mVideoWidget->eventKeyButtonPressed += MyGUI::newDelegate(this, &WindowManager::onVideoKeyPressed);
            mVideoBackground->eventKeyButtonPressed += MyGUI::newDelegate(this, &WindowManager::onVideoKeyPressed);
        }

        enableScene(false);

        MyGUI::IntSize screenSize = MyGUI::RenderManager::getInstance().getViewSize();
        sizeVideo(screenSize.width, screenSize.height);

        MyGUI::Widget* oldKeyFocus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        setKeyFocusWidget(mVideoWidget);

        mVideoBackground->setVisible(true);

        bool cursorWasVisible = mCursorVisible;
        setCursorVisible(false);

        if (mVideoWidget->hasAudioStream())
            MWBase::Environment::get().getSoundManager()->pauseSounds(MWSound::VideoPlayback,
                ~MWSound::Type::Movie & MWSound::Type::Mask
            );

        Misc::FrameRateLimiter frameRateLimiter = Misc::makeFrameRateLimiter(MWBase::Environment::get().getFrameRateLimit());
        while (mVideoWidget->update() && !MWBase::Environment::get().getStateManager()->hasQuitRequest())
        {
            const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(frameRateLimiter.getLastFrameDuration()).count();

            MWBase::Environment::get().getInputManager()->update(dt, true, false);

            if (!mWindowVisible)
            {
                mVideoWidget->pause();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            else
            {
                if (mVideoWidget->isPaused())
                    mVideoWidget->resume();

                mViewer->eventTraversal();
                mViewer->updateTraversal();
                mViewer->renderingTraversals();
            }
            // at the time this function is called we are in the middle of a frame,
            // so out of order calls are necessary to get a correct frameNumber for the next frame.
            // refer to the advance() and frame() order in Engine::go()
            mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());

            frameRateLimiter.limit();
        }
        mVideoWidget->stop();

        MWBase::Environment::get().getSoundManager()->resumeSounds(MWSound::VideoPlayback);

        setKeyFocusWidget(oldKeyFocus);

        setCursorVisible(cursorWasVisible);

        // Restore normal rendering
        updateVisible();

        mVideoBackground->setVisible(false);
    }

    void WindowManager::sizeVideo(int screenWidth, int screenHeight)
    {
        // Use black bars to correct aspect ratio
        bool stretch = Settings::Manager::getBool("stretch menu background", "GUI");
        mVideoBackground->setSize(screenWidth, screenHeight);
        mVideoWidget->autoResize(stretch);
    }

    void WindowManager::exitCurrentModal()
    {
        if (!mCurrentModals.empty())
        {
            WindowModal* window = mCurrentModals.back();
            if (!window->exit())
                return;
            window->setVisible(false);
        }
    }

    void WindowManager::addCurrentModal(WindowModal *input)
    {
        if (mCurrentModals.empty())
            mKeyboardNavigation->saveFocus(getMode());

        mCurrentModals.push_back(input);
        mKeyboardNavigation->restoreFocus(-1);

        mKeyboardNavigation->setModalWindow(input->mMainWidget);
        mKeyboardNavigation->setDefaultFocus(input->mMainWidget, input->getDefaultKeyFocus());
    }

    void WindowManager::removeCurrentModal(WindowModal* input)
    {
        if(!mCurrentModals.empty())
        {
            if(input == mCurrentModals.back())
            {
                mCurrentModals.pop_back();
                mKeyboardNavigation->saveFocus(-1);
            }
            else
            {
                auto found = std::find(mCurrentModals.begin(), mCurrentModals.end(), input);
                if (found != mCurrentModals.end())
                    mCurrentModals.erase(found);
                else
                    Log(Debug::Warning) << "Warning: can't find modal window " << input;
            }
        }
        if (mCurrentModals.empty())
        {
            mKeyboardNavigation->setModalWindow(nullptr);
            mKeyboardNavigation->restoreFocus(getMode());
        }
        else
            mKeyboardNavigation->setModalWindow(mCurrentModals.back()->mMainWidget);
    }

    void WindowManager::onVideoKeyPressed(MyGUI::Widget *_sender, MyGUI::KeyCode _key, MyGUI::Char _char)
    {
        if (_key == MyGUI::KeyCode::Escape)
            mVideoWidget->stop();
    }

    void WindowManager::updatePinnedWindows()
    {
        mInventoryWindow->setPinned(Settings::Manager::getBool("inventory pin", "Windows"));
        if (Settings::Manager::getBool("inventory hidden", "Windows"))
            mShown = (GuiWindow)(mShown ^ GW_Inventory);

        mMap->setPinned(Settings::Manager::getBool("map pin", "Windows"));
        if (Settings::Manager::getBool("map hidden", "Windows"))
            mShown = (GuiWindow)(mShown ^ GW_Map);

        mSpellWindow->setPinned(Settings::Manager::getBool("spells pin", "Windows"));
        if (Settings::Manager::getBool("spells hidden", "Windows"))
            mShown = (GuiWindow)(mShown ^ GW_Magic);

        mStatsWindow->setPinned(Settings::Manager::getBool("stats pin", "Windows"));
        if (Settings::Manager::getBool("stats hidden", "Windows"))
            mShown = (GuiWindow)(mShown ^ GW_Stats);
    }

    void WindowManager::pinWindow(GuiWindow window)
    {
        switch (window)
        {
        case GW_Inventory:
            mInventoryWindow->setPinned(true);
            break;
        case GW_Map:
            mMap->setPinned(true);
            break;
        case GW_Magic:
            mSpellWindow->setPinned(true);
            break;
        case GW_Stats:
            mStatsWindow->setPinned(true);
            break;
        default:
            break;
        }

        updateVisible();
    }

    void WindowManager::fadeScreenIn(const float time, bool clearQueue, float delay)
    {
        if (clearQueue)
            mScreenFader->clearQueue();
        mScreenFader->fadeOut(time, delay);
    }

    void WindowManager::fadeScreenOut(const float time, bool clearQueue, float delay)
    {
        if (clearQueue)
            mScreenFader->clearQueue();
        mScreenFader->fadeIn(time, delay);
    }

    void WindowManager::fadeScreenTo(const int percent, const float time, bool clearQueue, float delay)
    {
        if (clearQueue)
            mScreenFader->clearQueue();
        mScreenFader->fadeTo(percent, time, delay);
    }

    void WindowManager::setBlindness(const int percent)
    {
        mBlindnessFader->notifyAlphaChanged(percent / 100.f);
    }

    void WindowManager::activateHitOverlay(bool interrupt)
    {
        if (!mHitFaderEnabled)
            return;

        if (!interrupt && !mHitFader->isEmpty())
            return;

        mHitFader->clearQueue();
        mHitFader->fadeTo(100, 0.0f);
        mHitFader->fadeTo(0, 0.5f);
    }

    void WindowManager::setWerewolfOverlay(bool set)
    {
        if (!mWerewolfOverlayEnabled)
            return;

        if (mWerewolfFader)
            mWerewolfFader->notifyAlphaChanged(set ? 1.0f : 0.0f);
    }

    void WindowManager::onClipboardChanged(const std::string &_type, const std::string &_data)
    {
        if (_type == "Text")
            SDL_SetClipboardText(MyGUI::TextIterator::getOnlyText(MyGUI::UString(_data)).asUTF8().c_str());
    }

    void WindowManager::onClipboardRequested(const std::string &_type, std::string &_data)
    {
        if (_type != "Text")
            return;
        char* text=nullptr;
        text = SDL_GetClipboardText();
        if (text)
            _data = MyGUI::TextIterator::toTagsString(text);

        SDL_free(text);
    }

    void WindowManager::toggleConsole()
    {
        bool visible = mConsole->isVisible();

        if (!visible && !mGuiModes.empty())
            mKeyboardNavigation->saveFocus(mGuiModes.back());

        mConsole->setVisible(!visible);

        if (visible && !mGuiModes.empty())
            mKeyboardNavigation->restoreFocus(mGuiModes.back());

        updateVisible();
    }

    void WindowManager::toggleDebugWindow()
    {
#ifndef BT_NO_PROFILE
        mDebugWindow->setVisible(!mDebugWindow->isVisible());
#endif
    }

    void WindowManager::cycleSpell(bool next)
    {
        if (!isGuiMode())
            mSpellWindow->cycle(next);
    }

    void WindowManager::cycleWeapon(bool next)
    {
        if (!isGuiMode())
            mInventoryWindow->cycle(next);
    }

    void WindowManager::playSound(const std::string& soundId, float volume, float pitch)
    {
        if (soundId.empty())
            return;

        MWBase::Environment::get().getSoundManager()->playSound(soundId, volume, pitch, MWSound::Type::Sfx, MWSound::PlayMode::NoEnv);
    }

    void WindowManager::updateSpellWindow()
    {
        if (mSpellWindow)
            mSpellWindow->updateSpells();
    }

    void WindowManager::setConsoleSelectedObject(const MWWorld::Ptr &object)
    {
        mConsole->setSelectedObject(object);
    }

    /*
        Start of tes3mp addition

        Allow the direct setting of a console's Ptr, without the assumption that an object
        was clicked and that key focus should be restored to the console window, for console
        commands executed via server scripts
    */
    void WindowManager::setConsolePtr(const MWWorld::Ptr &object)
    {
        mConsole->setPtr(object);
    }
    /*
        End of tes3mp addition
    */

    /*
        Start of tes3mp addition

        Allow the clearing of the console's Ptr from elsewhere in the code, so that
        Ptrs used in console commands run from server scripts do not stay selected
    */
    void WindowManager::clearConsolePtr()
    {
        mConsole->resetReference();
    }
    /*
        End of tes3mp addition
    */

    std::string WindowManager::correctIconPath(const std::string& path)
    {
        return Misc::ResourceHelpers::correctIconPath(path, mResourceSystem->getVFS());
    }

    std::string WindowManager::correctBookartPath(const std::string& path, int width, int height, bool* exists)
    {
        std::string corrected = Misc::ResourceHelpers::correctBookartPath(path, width, height, mResourceSystem->getVFS());
        if (exists)
            *exists = mResourceSystem->getVFS()->exists(corrected);
        return corrected;
    }

    std::string WindowManager::correctTexturePath(const std::string& path)
    {
        return Misc::ResourceHelpers::correctTexturePath(path, mResourceSystem->getVFS());
    }

    bool WindowManager::textureExists(const std::string &path)
    {
        std::string corrected = Misc::ResourceHelpers::correctTexturePath(path, mResourceSystem->getVFS());
        return mResourceSystem->getVFS()->exists(corrected);
    }

    void WindowManager::createCursors()
    {
        // FIXME: currently we do not scale cursor since it is not a MyGUI widget.
        // In theory, we can do it manually (rescale the cursor image via osg::Imag::scaleImage() and scale the hotspot position).
        // Unfortunately, this apploach can lead to driver crashes on some setups (e.g. on laptops with nvidia-prime on Linux).
        MyGUI::ResourceManager::EnumeratorPtr enumerator = MyGUI::ResourceManager::getInstance().getEnumerator();
        while (enumerator.next())
        {
            MyGUI::IResource* resource = enumerator.current().second;
            ResourceImageSetPointerFix* imgSetPointer = resource->castType<ResourceImageSetPointerFix>(false);
            if (!imgSetPointer)
                continue;
            std::string tex_name = imgSetPointer->getImageSet()->getIndexInfo(0,0).texture;

            osg::ref_ptr<osg::Image> image = mResourceSystem->getImageManager()->getImage(tex_name);

            if(image.valid())
            {
                //everything looks good, send it to the cursor manager
                Uint8 hotspot_x = imgSetPointer->getHotSpot().left;
                Uint8 hotspot_y = imgSetPointer->getHotSpot().top;
                int rotation = imgSetPointer->getRotation();

                mCursorManager->createCursor(imgSetPointer->getResourceName(), rotation, image, hotspot_x, hotspot_y);
            }
        }
    }

    void WindowManager::createTextures()
    {
        {
            MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().createTexture("white");
            tex->createManual(8, 8, MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8);
            unsigned char* data = reinterpret_cast<unsigned char*>(tex->lock(MyGUI::TextureUsage::Write));
            for (int x=0; x<8; ++x)
                for (int y=0; y<8; ++y)
                {
                    *(data++) = 255;
                    *(data++) = 255;
                    *(data++) = 255;
                }
            tex->unlock();
        }

        {
            MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().createTexture("black");
            tex->createManual(8, 8, MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8);
            unsigned char* data = reinterpret_cast<unsigned char*>(tex->lock(MyGUI::TextureUsage::Write));
            for (int x=0; x<8; ++x)
                for (int y=0; y<8; ++y)
                {
                    *(data++) = 0;
                    *(data++) = 0;
                    *(data++) = 0;
                }
            tex->unlock();
        }

        {
            MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().createTexture("transparent");
            tex->createManual(8, 8, MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8A8);
            setMenuTransparency(Settings::Manager::getFloat("menu transparency", "GUI"));
        }
    }

    void WindowManager::setMenuTransparency(float value)
    {
        MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().getTexture("transparent");
        unsigned char* data = reinterpret_cast<unsigned char*>(tex->lock(MyGUI::TextureUsage::Write));
        for (int x=0; x<8; ++x)
            for (int y=0; y<8; ++y)
            {
                *(data++) = 255;
                *(data++) = 255;
                *(data++) = 255;
                *(data++) = static_cast<unsigned char>(value*255);
            }
        tex->unlock();
    }

    void WindowManager::addCell(MWWorld::CellStore* cell)
    {
        mLocalMapRender->addCell(cell);
    }

    void WindowManager::removeCell(MWWorld::CellStore *cell)
    {
        mLocalMapRender->removeCell(cell);
    }

    void WindowManager::writeFog(MWWorld::CellStore *cell)
    {
        mLocalMapRender->saveFogOfWar(cell);
    }

    const MWGui::TextColours& WindowManager::getTextColours()
    {
        return mTextColours;
    }

    void WindowManager::setPhysicsGrabHint(bool visible, int moveMode, bool physicsEnabled)
    {
        if (!mPhysicsGrabHint || !mPhysicsGrabHintText)
            return;

        if (!visible)
        {
            mPhysicsGrabHint->setVisible(false);
            return;
        }

        const int safeMode = std::max(0, std::min(3, moveMode));
        const char* modeKeys[] = {
            "placement.mode.walk", "placement.mode.xy", "placement.mode.xz", "placement.mode.zy"
        };

        auto arenaText = [](const std::string& key)
        {
            return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
        };

        std::string caption = arenaText("placement.release") + "\n"
            + arenaText("placement.physics") + ": "
            + arenaText(physicsEnabled ? "placement.on" : "placement.off") + "\n"
            + arenaText("placement.rotate") + "\n"
            + arenaText("placement.reset") + "\n"
            + arenaText("placement.mode") + ": " + arenaText(modeKeys[safeMode]);

        if (safeMode != 0)
            caption += "\n" + arenaText("placement.move");

        // Match the Z animation menu width and left offset, but sit slightly
        // below vertical centre so it does not compete with the crosshair/object.
        const int width = 310;
        const int height = safeMode == 0 ? 166 : 185;
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        const int left = std::max(14, static_cast<int>(view.width * 0.035f));
        const int top = std::max(14, (view.height - height) / 2 + std::max(24, static_cast<int>(view.height * 0.055f)));
        mPhysicsGrabHint->setCoord(left, top, width, height);
        mPhysicsGrabHintText->setCoord(12, 34, width - 24, height - 42);
        mPhysicsGrabHintText->setCaption(caption);
        mPhysicsGrabHint->setVisible(true);
    }

    bool WindowManager::injectKeyPress(MyGUI::KeyCode key, unsigned int text, bool repeat)
    {
        if (getMode() == GM_Dialogue && mDialogueWindow && mDialogueWindow->handleKeyPress(key, repeat))
            return true;

        if (!mKeyboardNavigation->injectKeyPress(key, text, repeat))
        {
            MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
            bool widgetActive = MyGUI::InputManager::getInstance().injectKeyPress(key, text);
            if (!widgetActive || !focus)
                return false;
            // FIXME: MyGUI doesn't allow widgets to state if a given key was actually used, so make a guess
            if (focus->getTypeName().find("Button") != std::string::npos)
            {
                switch (key.getValue())
                {
                case MyGUI::KeyCode::ArrowDown:
                case MyGUI::KeyCode::ArrowUp:
                case MyGUI::KeyCode::ArrowLeft:
                case MyGUI::KeyCode::ArrowRight:
                case MyGUI::KeyCode::Return:
                case MyGUI::KeyCode::NumpadEnter:
                case MyGUI::KeyCode::Space:
                    return true;
                default:
                    return false;
                }
            }
            return false;
        }
        else
            return true;
    }

    bool WindowManager::injectKeyRelease(MyGUI::KeyCode key)
    {
        return MyGUI::InputManager::getInstance().injectKeyRelease(key);
    }

    void WindowManager::GuiModeState::update(bool visible)
    {
        for (unsigned int i=0; i<mWindows.size(); ++i)
            mWindows[i]->setVisible(visible);
    }

    void WindowManager::watchActor(const MWWorld::Ptr& ptr)
    {
        mStatsWatcher->watchActor(ptr);
    }

    MWWorld::Ptr WindowManager::getWatchedActor() const
    {
        return mStatsWatcher->getWatchedActor();
    }
}
