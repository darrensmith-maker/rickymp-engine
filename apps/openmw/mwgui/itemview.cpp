#include "itemview.hpp"

#include <vector>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <MyGUI_Button.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_KeyCode.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"

#include "itemmodel.hpp"
#include "itemwidget.hpp"
#include "sortfilteritemmodel.hpp"

namespace
{
    std::string arenaInventoryText(const std::string& key)
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string formatWeight(float value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(value < 10.f ? 2 : 1) << value;
        std::string result = stream.str();
        while (result.size() > 1 && result.back() == '0')
            result.pop_back();
        if (!result.empty() && result.back() == '.')
            result.pop_back();
        return result;
    }

    // Keep the table header and every item row on exactly the same column grid.
    // Earlier cumulative builds used 60px for the toolbar controls but only
    // 36px for the row icon, so headers and cells could never line up.
    constexpr int sListRowHeight = 36;
    constexpr int sListIconColumnWidth = 40;
    constexpr int sListCountWidth = 54;
    constexpr int sListWeightWidth = 58;
    constexpr int sListValueWidth = 60;

    bool isVirtualKeyRing(const MWGui::ItemStack& item)
    {
        return item.mCreator == nullptr && !item.mBase.isEmpty()
            && item.mBase.getClass().isKey(item.mBase);
    }

    struct KeyRingSummary
    {
        size_t mCount = 0;
        float mWeight = 0.f;
        int mValue = 0;
    };

    KeyRingSummary getKeyRingSummary(const MWGui::ItemStack& item)
    {
        KeyRingSummary summary;
        summary.mCount = item.mCount;

        MWWorld::ContainerStore* store = item.mBase.getContainerStore();
        if (!store)
        {
            summary.mWeight = item.mBase.getClass().getWeight(item.mBase) * static_cast<float>(item.mCount);
            summary.mValue = item.mBase.getClass().getValue(item.mBase) * static_cast<int>(item.mCount);
            return summary;
        }

        summary.mCount = 0;
        for (MWWorld::ContainerStoreIterator it = store->begin(MWWorld::ContainerStore::Type_Miscellaneous);
             it != store->end(); ++it)
        {
            MWWorld::Ptr key = *it;
            if (!key.getClass().isKey(key))
                continue;

            const int count = std::max(0, key.getRefData().getCount());
            if (count == 0)
                continue;

            summary.mCount += static_cast<size_t>(count);
            summary.mWeight += std::max(0.f, key.getClass().getWeight(key)) * static_cast<float>(count);
            summary.mValue += std::max(0, key.getClass().getValue(key)) * count;
        }
        return summary;
    }
}

namespace MWGui
{

    namespace
    {
        std::vector<ItemView*> sExtendedItemViews;
    }


ItemView::ItemView()
    : mModel(nullptr)
    , mScrollView(nullptr)
    , mListScrollView(nullptr)
    , mToolbar(nullptr)
    , mViewModeButton(nullptr)
    , mViewModeIcon(nullptr)
    , mDefaultSortButton(nullptr)
    , mDefaultSortIcon(nullptr)
    , mNameHeader(nullptr)
    , mCountHeader(nullptr)
    , mWeightHeader(nullptr)
    , mValueHeader(nullptr)
    , mNameSortIcon(nullptr)
    , mCountSortIcon(nullptr)
    , mWeightSortIcon(nullptr)
    , mValueSortIcon(nullptr)
    , mExtendedMode(false)
    , mInternalViewModeButtonVisible(true)
    , mSingleClickActionEnabled(false)
    , mViewMode(View_List)
    , mListPressedIndex(-1)
    , mListDragStartX(0)
    , mListDragStartY(0)
    , mListDragStarted(false)
    , mKeyboardFocusedIndex(-1)
{
    sExtendedItemViews.push_back(this);
}

ItemView::~ItemView()
{
    sExtendedItemViews.erase(std::remove(sExtendedItemViews.begin(), sExtendedItemViews.end(), this),
        sExtendedItemViews.end());
    delete mModel;
}

void ItemView::setModel(ItemModel *model)
{
    if (mModel == model)
        return;

    delete mModel;
    mModel = model;
    mKeyboardFocusedIndex = -1;

    updateHeaderCaptions();
    update();
}

void ItemView::initialiseOverride()
{
    Base::initialiseOverride();
    setNeedKeyFocus(true);

    assignWidget(mScrollView, "ScrollView");
    if (mScrollView == nullptr)
        throw std::runtime_error("Item view needs a scroll view");

    mScrollView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);

    // The classic ItemView skin owns a horizontal-only scroll view.  The
    // Inventory Extender table needs an independent vertical scroll view, so
    // keep both and switch without changing the widget skin at runtime.
    mListScrollView = createWidget<MyGUI::ScrollView>("MW_ScrollView",
        MyGUI::IntCoord(3, 31, std::max(1, getWidth() - 6), std::max(1, getHeight() - 34)),
        MyGUI::Align::Stretch, "ExtendedListScroll");
    mListScrollView->setCanvasAlign(MyGUI::Align::Left | MyGUI::Align::Top);
    mListScrollView->setVisible(false);

    mToolbar = createWidget<MyGUI::Widget>("",
        MyGUI::IntCoord(3, 3, std::max(1, getWidth() - 6), 25),
        MyGUI::Align::Top | MyGUI::Align::HStretch, "ExtendedToolbar");

    mViewModeButton = mToolbar->createWidget<MyGUI::Button>("MW_Button",
        MyGUI::IntCoord(0, 0, 28, 24), MyGUI::Align::Left | MyGUI::Align::Top, "ExtendedViewMode");
    mViewModeButton->setNeedKeyFocus(false);
    mViewModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onToggleView);
    mViewModeIcon = mViewModeButton->createWidget<MyGUI::ImageBox>("ImageBox",
        MyGUI::IntCoord(6, 4, 16, 16), MyGUI::Align::Center, "ExtendedViewModeIcon");
    mViewModeIcon->setNeedMouseFocus(false);

    mDefaultSortButton = mToolbar->createWidget<MyGUI::Button>("MW_Button",
        MyGUI::IntCoord(30, 0, 28, 24), MyGUI::Align::Left | MyGUI::Align::Top, "ExtendedDefaultSort");
    mDefaultSortButton->setNeedKeyFocus(false);
    mDefaultSortButton->setCaption("");
    mDefaultSortButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSortDefault);
    mDefaultSortIcon = mDefaultSortButton->createWidget<MyGUI::ImageBox>("ImageBox",
        MyGUI::IntCoord(6, 4, 16, 16), MyGUI::Align::Center, "ExtendedDefaultSortIcon");
    mDefaultSortIcon->setNeedMouseFocus(false);
    mDefaultSortIcon->setImageTexture("icons/inventoryextender/Base/default_sort.dds");

    mNameHeader = mToolbar->createWidget<MyGUI::Button>("MW_Button",
        MyGUI::IntCoord(60, 0, 150, 24), MyGUI::Align::Left | MyGUI::Align::Top, "ExtendedNameHeader");
    mCountHeader = mToolbar->createWidget<MyGUI::Button>("MW_Button",
        MyGUI::IntCoord(182, 0, 46, 24), MyGUI::Align::Right | MyGUI::Align::Top, "ExtendedCountHeader");
    mWeightHeader = mToolbar->createWidget<MyGUI::Button>("MW_Button",
        MyGUI::IntCoord(230, 0, 56, 24), MyGUI::Align::Right | MyGUI::Align::Top, "ExtendedWeightHeader");
    mValueHeader = mToolbar->createWidget<MyGUI::Button>("MW_Button",
        MyGUI::IntCoord(288, 0, 58, 24), MyGUI::Align::Right | MyGUI::Align::Top, "ExtendedValueHeader");

    mNameHeader->setNeedKeyFocus(false);
    mCountHeader->setNeedKeyFocus(false);
    mWeightHeader->setNeedKeyFocus(false);
    mValueHeader->setNeedKeyFocus(false);
    mNameHeader->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSortName);
    mCountHeader->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSortCount);
    mWeightHeader->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSortWeight);
    mValueHeader->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSortValue);

    // Numeric headers and cells use the exact same centred alignment. This is
    // deliberately explicit instead of relying on the MW_Button/SandText skins,
    // whose text margins differ slightly and made values look offset from their
    // column titles.
    mCountHeader->setTextAlign(MyGUI::Align::Center);
    mWeightHeader->setTextAlign(MyGUI::Align::Center);
    mValueHeader->setTextAlign(MyGUI::Align::Center);

    const auto createSortIcon = [](MyGUI::Button* parent, const std::string& name)
    {
        MyGUI::ImageBox* icon = parent->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(std::max(0, parent->getWidth() - 18), 4, 14, 14),
            MyGUI::Align::Right | MyGUI::Align::VCenter, name);
        icon->setNeedMouseFocus(false);
        icon->setVisible(false);
        return icon;
    };
    mNameSortIcon = createSortIcon(mNameHeader, "ExtendedNameSortIcon");
    mCountSortIcon = createSortIcon(mCountHeader, "ExtendedCountSortIcon");
    mWeightSortIcon = createSortIcon(mWeightHeader, "ExtendedWeightSortIcon");
    mValueSortIcon = createSortIcon(mValueHeader, "ExtendedValueSortIcon");

    mToolbar->setVisible(false);
    updateHeaderCaptions();
    updateExtendedGeometry();
}

void ItemView::setExtendedMode(bool enabled)
{
    if (mExtendedMode == enabled)
        return;
    mExtendedMode = enabled;
    if (mExtendedMode)
        mViewMode = View_List;
    updateExtendedGeometry();
    updateHeaderCaptions();
    update();
}

void ItemView::setViewMode(ViewMode mode)
{
    if (!mExtendedMode || mViewMode == mode)
        return;
    mViewMode = mode;
    updateExtendedGeometry();
    updateHeaderCaptions();
    update();
    resetScrollBars();
}

void ItemView::setInternalViewModeButtonVisible(bool visible)
{
    if (mInternalViewModeButtonVisible == visible)
        return;

    mInternalViewModeButtonVisible = visible;
    updateExtendedGeometry();
    updateHeaderCaptions();
}

MyGUI::ScrollView* ItemView::getActiveScrollView() const
{
    if (mExtendedMode && mViewMode == View_List)
        return mListScrollView;
    return mScrollView;
}

void ItemView::clearScrollView(MyGUI::ScrollView* view)
{
    if (!view)
        return;
    while (view->getChildCount())
        MyGUI::Gui::getInstance().destroyWidget(view->getChildAt(0));
}

void ItemView::updateExtendedGeometry()
{
    if (!mScrollView || !mListScrollView || !mToolbar)
        return;

    const int width = std::max(1, getWidth());
    const int height = std::max(1, getHeight());

    if (!mExtendedMode)
    {
        mToolbar->setVisible(false);
        mListScrollView->setVisible(false);
        mScrollView->setVisible(true);
        mScrollView->setCoord(3, 3, std::max(1, width - 6), std::max(1, height - 6));
        return;
    }

    const int toolbarHeight = 25;
    mToolbar->setVisible(true);
    mToolbar->setCoord(3, 3, std::max(1, width - 6), toolbarHeight);

    const bool list = mViewMode == View_List;
    mScrollView->setVisible(!list);
    mListScrollView->setVisible(list);

    MyGUI::ScrollView* active = list ? mListScrollView : mScrollView;
    active->setCoord(3, toolbarHeight + 5, std::max(1, width - 6), std::max(1, height - toolbarHeight - 8));

    const int toolbarWidth = mToolbar->getWidth();
    const int modeWidth = sListIconColumnWidth / 2;
    const int defaultSortWidth = sListIconColumnWidth - modeWidth;
    const int nameLeft = sListIconColumnWidth;
    const int nameWidth = std::max(55, toolbarWidth - nameLeft
        - sListCountWidth - sListWeightWidth - sListValueWidth);
    int x = nameLeft;
    if (mInternalViewModeButtonVisible)
    {
        mViewModeButton->setVisible(true);
        mViewModeButton->setCoord(0, 0, modeWidth, 24);
        mDefaultSortButton->setCoord(modeWidth, 0, defaultSortWidth, 24);
        if (mViewModeIcon)
            mViewModeIcon->setCoord(3, 5, 14, 14);
        if (mDefaultSortIcon)
            mDefaultSortIcon->setCoord(3, 5, 14, 14);
    }
    else
    {
        // The host window owns the table/grid toggle. Keep only the default
        // sort action in the icon column so the top row contains sorting only.
        mViewModeButton->setVisible(false);
        mDefaultSortButton->setCoord(0, 0, sListIconColumnWidth, 24);
        if (mDefaultSortIcon)
            mDefaultSortIcon->setCoord(
                std::max(0, (sListIconColumnWidth - 14) / 2), 5, 14, 14);
    }
    mNameHeader->setCoord(x, 0, nameWidth, 24);
    x += nameWidth;
    mCountHeader->setCoord(x, 0, sListCountWidth, 24);
    x += sListCountWidth;
    mWeightHeader->setCoord(x, 0, sListWeightWidth, 24);
    x += sListWeightWidth;
    mValueHeader->setCoord(x, 0, std::max(36, toolbarWidth - x), 24);

    const auto placeSortIcon = [](MyGUI::ImageBox* icon, MyGUI::Button* parent)
    {
        if (icon && parent)
            icon->setCoord(std::max(0, parent->getWidth() - 18), 5, 14, 14);
    };
    placeSortIcon(mNameSortIcon, mNameHeader);
    placeSortIcon(mCountSortIcon, mCountHeader);
    placeSortIcon(mWeightSortIcon, mWeightHeader);
    placeSortIcon(mValueSortIcon, mValueHeader);

    mDefaultSortButton->setVisible(list);
    mNameHeader->setVisible(list);
    mCountHeader->setVisible(list);
    mWeightHeader->setVisible(list);
    mValueHeader->setVisible(list);
}

void ItemView::updateHeaderIcons()
{
    if (!mViewModeIcon)
        return;

    static const std::string iconRoot = "icons/inventoryextender/Base/";
    mViewModeIcon->setImageTexture(iconRoot + (mViewMode == View_List ? "view_grid.dds" : "view_table.dds"));

    MyGUI::ImageBox* sortIcons[] = { mNameSortIcon, mCountSortIcon, mWeightSortIcon, mValueSortIcon };
    for (MyGUI::ImageBox* icon : sortIcons)
    {
        if (icon)
        {
            icon->setVisible(false);
            icon->setImageTexture("");
        }
    }

    SortFilterItemModel* sort = dynamic_cast<SortFilterItemModel*>(mModel);
    if (!sort)
        return;

    MyGUI::ImageBox* active = nullptr;
    switch (sort->getSortMode())
    {
        case SortFilterItemModel::Sort_Name: active = mNameSortIcon; break;
        case SortFilterItemModel::Sort_Count: active = mCountSortIcon; break;
        case SortFilterItemModel::Sort_Weight: active = mWeightSortIcon; break;
        case SortFilterItemModel::Sort_Value: active = mValueSortIcon; break;
        default: break;
    }

    if (active)
    {
        active->setImageTexture(iconRoot + (sort->getSortAscending() ? "sort_asc.dds" : "sort_desc.dds"));
        active->setVisible(true);
    }
}

void ItemView::updateHeaderCaptions()
{
    if (!mViewModeButton || !mNameHeader)
        return;

    // Inventory Extender uses texture icons for view/sort state.  Keep the
    // localized column names clean instead of appending ASCII ^/v markers.
    mViewModeButton->setCaption("");
    mNameHeader->setCaption(arenaInventoryText("inventoryext.name"));
    mCountHeader->setCaption(arenaInventoryText("inventoryext.count"));
    mWeightHeader->setCaption(arenaInventoryText("inventoryext.weight"));
    mValueHeader->setCaption(arenaInventoryText("inventoryext.value"));
    updateHeaderIcons();
}

void ItemView::layoutGridWidgets(MyGUI::ScrollView* view)
{
    if (!view || !view->getChildCount())
        return;

    int x = 0;
    int y = 0;
    MyGUI::Widget* dragArea = view->getChildAt(0);
    int maxHeight = view->getHeight();

    int rows = maxHeight/42;
    rows = std::max(rows, 1);
    bool showScrollbar = int(std::ceil(dragArea->getChildCount()/float(rows))) > view->getWidth()/42;
    if (showScrollbar)
        maxHeight -= 18;

    for (unsigned int i=0; i<dragArea->getChildCount(); ++i)
    {
        MyGUI::Widget* w = dragArea->getChildAt(i);
        w->setPosition(x, y);
        y += 42;
        if (y > maxHeight-42 && i < dragArea->getChildCount()-1)
        {
            x += 42;
            y = 0;
        }
    }
    x += 42;

    MyGUI::IntSize size = MyGUI::IntSize(std::max(view->getSize().width, x), view->getSize().height);
    view->setVisibleVScroll(false);
    view->setVisibleHScroll(false);
    view->setCanvasSize(size);
    view->setVisibleVScroll(true);
    view->setVisibleHScroll(true);
    dragArea->setSize(size);
}

void ItemView::layoutListWidgets(MyGUI::ScrollView* view)
{
    if (!view || !view->getChildCount())
        return;

    MyGUI::Widget* dragArea = view->getChildAt(0);
    const int rowCount = static_cast<int>(dragArea->getChildCount());

    // MyGUI 0.47 can report a stale ScrollView::getClientCoord() immediately
    // after a parent/pane resize.  In practice this left the table at its old
    // narrow width while the Inventory Extender pane itself had already grown.
    // Use the real widget dimensions as the source of truth and reserve the
    // scrollbar gutter explicitly. This makes the columns fill the whole pane
    // on the very first layout pass and after paper-doll/window changes.
    constexpr int scrollBarGutter = 18;
    constexpr int itemViewInnerMargin = 6;
    constexpr int toolbarHeight = 25;

    // Force the internal list viewport to the *current* ItemView dimensions.
    // In MyGUI 0.47 a child created with Align::Stretch may keep its template
    // width until the next layout event even though ItemView has already been
    // resized by InventoryWindow::adjustPanes().  That produced the large empty
    // strip on the right while all columns remained packed into ~200px.
    const int viewportWidth = std::max(1, getWidth() - itemViewInnerMargin);
    const int viewportHeight = std::max(1, getHeight() - toolbarHeight - 8);
    view->setCoord(3, toolbarHeight + 5, viewportWidth, viewportHeight);

    const int canvasHeight = std::max(viewportHeight, rowCount * sListRowHeight);
    const bool needsVScroll = rowCount * sListRowHeight > viewportHeight;

    view->setVisibleHScroll(false);
    view->setVisibleVScroll(needsVScroll);

    // Never use ScrollView::getClientCoord().width as a source of truth here:
    // on MyGUI 0.47 it can still describe the previous canvas/viewport size.
    // The item view width is authoritative; only reserve the V-scroll gutter.
    const int rowWidth = std::max(180, viewportWidth - (needsVScroll ? scrollBarGutter : 0));
    const int nameWidth = std::max(60, rowWidth - sListIconColumnWidth
        - sListCountWidth - sListWeightWidth - sListValueWidth);

    // Header and rows share exactly the same calculated table width.  Keep the
    // toolbar widget itself full-width so its skin/background reaches the right
    // edge; only the column grid stops before the vertical scrollbar gutter.
    if (mToolbar)
    {
        mToolbar->setCoord(3, 3, viewportWidth, toolbarHeight);
        const int modeWidth = sListIconColumnWidth / 2;
        int x = sListIconColumnWidth;
        if (mInternalViewModeButtonVisible)
        {
            mViewModeButton->setVisible(true);
            mViewModeButton->setCoord(0, 0, modeWidth, 24);
            mDefaultSortButton->setCoord(modeWidth, 0, sListIconColumnWidth - modeWidth, 24);
        }
        else
        {
            mViewModeButton->setVisible(false);
            mDefaultSortButton->setCoord(0, 0, sListIconColumnWidth, 24);
        }
        mNameHeader->setCoord(x, 0, nameWidth, 24); x += nameWidth;
        mCountHeader->setCoord(x, 0, sListCountWidth, 24); x += sListCountWidth;
        mWeightHeader->setCoord(x, 0, sListWeightWidth, 24); x += sListWeightWidth;
        mValueHeader->setCoord(x, 0, std::max(36, rowWidth - x), 24);
    }

    dragArea->setAlign(MyGUI::Align::Default);
    dragArea->setCoord(0, 0, rowWidth, canvasHeight);

    int y = 0;
    for (unsigned int i = 0; i < dragArea->getChildCount(); ++i)
    {
        MyGUI::Widget* row = dragArea->getChildAt(i);
        // Rows use absolute table coordinates. HStretch here makes MyGUI apply a
        // second width delta when the canvas is resized, leaving the children at
        // stale coordinates and producing the smeared/overlapping list seen in 009.
        row->setAlign(MyGUI::Align::Default);
        row->setCoord(0, y, rowWidth, sListRowHeight);
        if (row->getChildCount() >= 5)
        {
            row->getChildAt(0)->setCoord(4, 2, 32, 32);
            row->getChildAt(1)->setCoord(sListIconColumnWidth, 0, nameWidth, sListRowHeight);
            int x = sListIconColumnWidth + nameWidth;
            row->getChildAt(2)->setCoord(x, 0, sListCountWidth, sListRowHeight); x += sListCountWidth;
            row->getChildAt(3)->setCoord(x, 0, sListWeightWidth, sListRowHeight); x += sListWeightWidth;
            row->getChildAt(4)->setCoord(x, 0, std::max(1, rowWidth - x), sListRowHeight);
        }
        y += sListRowHeight;
    }

    view->setCanvasSize(MyGUI::IntSize(rowWidth, canvasHeight));
}

void ItemView::layoutWidgets()
{
    if (mExtendedMode && mViewMode == View_List)
        layoutListWidgets(mListScrollView);
    else
        layoutGridWidgets(mScrollView);
}

void ItemView::update()
{
    // Parent panes can be resized by MyGUI alignment without going through our
    // setSize override. Always consume the final geometry before creating rows.
    updateExtendedGeometry();

    clearScrollView(mScrollView);
    clearScrollView(mListScrollView);

    if (!mModel)
        return;

    mModel->update();
    updateHeaderCaptions();

    MyGUI::ScrollView* view = getActiveScrollView();
    const bool list = mExtendedMode && mViewMode == View_List;
    MyGUI::Widget* dragArea = view->createWidget<MyGUI::Widget>("", 0, 0, view->getWidth(), view->getHeight(),
        list ? MyGUI::Align::Default : MyGUI::Align::Stretch);
    dragArea->setNeedMouseFocus(true);
    dragArea->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSelectedBackground);
    dragArea->eventMouseWheel += MyGUI::newDelegate(this, &ItemView::onMouseWheelMoved);
    for (ItemModel::ModelIndex i=0; i<static_cast<int>(mModel->getItemCount()); ++i)
    {
        const ItemStack& item = mModel->getItem(i);
        ItemWidget::ItemState state = ItemWidget::None;
        if (item.mType == ItemStack::Type_Barter)
            state = ItemWidget::Barter;
        if (item.mType == ItemStack::Type_Equipped)
            state = ItemWidget::Equip;

        if (!list)
        {
            ItemWidget* itemWidget = dragArea->createWidget<ItemWidget>("MW_ItemIcon",
                MyGUI::IntCoord(0, 0, 42, 42), MyGUI::Align::Default);
            itemWidget->setUserString("ToolTipType", "ItemModelIndex");
            itemWidget->setUserData(std::make_pair(i, mModel));
            itemWidget->setItem(item.mBase, state);
            itemWidget->setCount(item.mCount);
            itemWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemView::onSelectedItem);
            itemWidget->eventMouseWheel += MyGUI::newDelegate(this, &ItemView::onMouseWheelMoved);
            continue;
        }

        MyGUI::Button* row = dragArea->createWidget<MyGUI::Button>("MW_ListLine",
            MyGUI::IntCoord(0, 0, view->getWidth(), 36), MyGUI::Align::Default);
        row->setNeedKeyFocus(false);
        row->setUserString("ToolTipType", "ItemModelIndex");
        row->setUserData(std::make_pair(i, mModel));
        // Extended list interaction follows modern inventory behaviour:
        // single click focuses, dragging starts vanilla drag-and-drop, while a
        // double click activates/equips the item. This avoids the first click of
        // a double click immediately turning into a floating drag icon.
        row->eventMouseButtonPressed += MyGUI::newDelegate(this, &ItemView::onListItemPressed);
        row->eventMouseDrag += MyGUI::newDelegate(this, &ItemView::onListItemDragged);
        row->eventMouseButtonReleased += MyGUI::newDelegate(this, &ItemView::onListItemReleased);
        row->eventMouseButtonDoubleClick += MyGUI::newDelegate(this, &ItemView::onListItemDoubleClicked);
        row->eventMouseWheel += MyGUI::newDelegate(this, &ItemView::onMouseWheelMoved);

        ItemWidget* icon = row->createWidget<ItemWidget>("MW_ItemIcon", MyGUI::IntCoord(2, 2, 32, 32), MyGUI::Align::Left);
        icon->setUserString("ToolTipType", "ItemModelIndex");
        icon->setUserData(std::make_pair(i, mModel));
        icon->setItem(item.mBase, state);
        icon->setCount(item.mCount);
        icon->eventMouseButtonPressed += MyGUI::newDelegate(this, &ItemView::onListItemPressed);
        icon->eventMouseDrag += MyGUI::newDelegate(this, &ItemView::onListItemDragged);
        icon->eventMouseButtonReleased += MyGUI::newDelegate(this, &ItemView::onListItemReleased);
        icon->eventMouseButtonDoubleClick += MyGUI::newDelegate(this, &ItemView::onListItemDoubleClicked);
        icon->eventMouseWheel += MyGUI::newDelegate(this, &ItemView::onMouseWheelMoved);

        MyGUI::TextBox* name = row->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(), MyGUI::Align::Default);
        MyGUI::TextBox* count = row->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(), MyGUI::Align::Default);
        MyGUI::TextBox* weight = row->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(), MyGUI::Align::Default);
        MyGUI::TextBox* value = row->createWidget<MyGUI::TextBox>("SandText", MyGUI::IntCoord(), MyGUI::Align::Default);
        name->setNeedMouseFocus(false);
        count->setNeedMouseFocus(false);
        weight->setNeedMouseFocus(false);
        value->setNeedMouseFocus(false);
        // Table cells are single-line by design. If the ItemView is created at
        // its small layout-template size, automatic word wrapping otherwise
        // survives into the first visible frame and draws two lines on top of
        // neighbouring columns.
        name->getSubWidgetText()->setWordWrap(false);
        count->getSubWidgetText()->setWordWrap(false);
        weight->getSubWidgetText()->setWordWrap(false);
        value->getSubWidgetText()->setWordWrap(false);
        name->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
        count->setTextAlign(MyGUI::Align::Center);
        weight->setTextAlign(MyGUI::Align::Center);
        value->setTextAlign(MyGUI::Align::Center);
        if (isVirtualKeyRing(item))
        {
            const KeyRingSummary summary = getKeyRingSummary(item);
            name->setCaption(arenaInventoryText("keyring.title"));
            count->setCaption(MyGUI::utility::toString(summary.mCount));
            weight->setCaption(formatWeight(summary.mWeight));
            value->setCaption(MyGUI::utility::toString(summary.mValue));
        }
        else
        {
            name->setCaption(item.mBase.getClass().getName(item.mBase));
            count->setCaption(MyGUI::utility::toString(item.mCount));
            weight->setCaption(formatWeight(item.mBase.getClass().getWeight(item.mBase)));
            value->setCaption(MyGUI::utility::toString(item.mBase.getClass().getValue(item.mBase)));
        }
    }

    layoutWidgets();
    if (mKeyboardFocusedIndex >= 0 && mModel->getItemCount() > 0)
        forceItemFocused(mKeyboardFocusedIndex);
}

int ItemView::forceItemFocused(int index)
{
    MyGUI::ScrollView* view = getActiveScrollView();
    if (!mModel || !view || !view->getChildCount() || mModel->getItemCount() == 0)
        return 0;

    const int count = static_cast<int>(mModel->getItemCount());
    index = std::max(0, std::min(index, count - 1));
    mKeyboardFocusedIndex = index;

    MyGUI::Widget* dragArea = view->getChildAt(0);
    const unsigned int childCount = dragArea->getChildCount();
    for (unsigned int i = 0; i < childCount; ++i)
        dragArea->getChildAt(i)->setAlpha(static_cast<int>(i) == index ? 1.f : 0.65f);

    if (static_cast<unsigned int>(index) < childCount)
    {
        MyGUI::Widget* focused = dragArea->getChildAt(static_cast<unsigned int>(index));
        MyGUI::IntPoint offset = view->getViewOffset();
        if (mExtendedMode && mViewMode == View_List)
        {
            const int focusedTop = focused->getTop() + offset.top;
            const int focusedBottom = focusedTop + focused->getHeight();
            if (focusedTop < 0)
                offset.top -= focusedTop;
            else if (focusedBottom > view->getHeight())
                offset.top -= focusedBottom - view->getHeight();
            offset.top = std::min(0, offset.top);
        }
        else
        {
            const int focusedLeft = focused->getLeft() + offset.left;
            const int focusedRight = focusedLeft + focused->getWidth();
            if (focusedLeft < 0)
                offset.left -= focusedLeft;
            else if (focusedRight > view->getWidth())
                offset.left -= focusedRight - view->getWidth();
            offset.left = std::min(0, offset.left);
        }
        view->setViewOffset(offset);
        sExtendedItemViews.push_back(this);
    }

    return index;
}

bool ItemView::handleNavigationKey(MyGUI::KeyCode key)
{
    if (!mModel || mModel->getItemCount() == 0)
        return false;

    const int count = static_cast<int>(mModel->getItemCount());
    int index = mKeyboardFocusedIndex < 0 ? 0 : std::min(mKeyboardFocusedIndex, count - 1);

    if (key == MyGUI::KeyCode::Return || key == MyGUI::KeyCode::NumpadEnter
        || key == MyGUI::KeyCode::Space)
    {
        forceItemFocused(index);
        if (mExtendedMode && mViewMode == View_List && !mSingleClickActionEnabled)
            eventItemDoubleClicked(index);
        else
            eventItemClicked(index);
        return true;
    }

    int delta = 0;
    if (mExtendedMode && mViewMode == View_List)
    {
        if (key == MyGUI::KeyCode::ArrowUp || key == MyGUI::KeyCode::ArrowLeft) delta = -1;
        if (key == MyGUI::KeyCode::ArrowDown || key == MyGUI::KeyCode::ArrowRight) delta = 1;
    }
    else
    {
        MyGUI::ScrollView* view = getActiveScrollView();
        int rows = view ? std::max(1, view->getHeight() / 42) : 1;
        if (key == MyGUI::KeyCode::ArrowUp) delta = -1;
        if (key == MyGUI::KeyCode::ArrowDown) delta = 1;
        if (key == MyGUI::KeyCode::ArrowLeft) delta = -rows;
        if (key == MyGUI::KeyCode::ArrowRight) delta = rows;
    }

    if (delta == 0)
        return false;

    index = std::max(0, std::min(count - 1, index + delta));
    forceItemFocused(index);
    return true;
}

int ItemView::requestListSize() const
{
    if (!mModel)
        return 0;

    if (mExtendedMode && mViewMode == View_List)
        return static_cast<int>(mModel->getItemCount()) * sListRowHeight + 31;

    constexpr int itemSize = 42;
    constexpr int innerMargin = 6;
    const int count = static_cast<int>(mModel->getItemCount());
    const int usableWidth = std::max(itemSize, getWidth() - innerMargin);
    const int columns = std::max(1, usableWidth / itemSize);
    const int rows = std::max(1, (count + columns - 1) / columns);
    return rows * itemSize + innerMargin;
}

void ItemView::relayout()
{
    updateExtendedGeometry();
    layoutWidgets();
}

void ItemView::resetScrollBars()
{
    if (mScrollView)
        mScrollView->setViewOffset(MyGUI::IntPoint(0, 0));
    if (mListScrollView)
        mListScrollView->setViewOffset(MyGUI::IntPoint(0, 0));
}

void ItemView::onSelectedItem(MyGUI::Widget *sender)
{
    ItemModel::ModelIndex index = (*sender->getUserData<std::pair<ItemModel::ModelIndex, ItemModel*> >()).first;
    eventItemClicked(index);
}

void ItemView::onListItemPressed(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
{
    if (id != MyGUI::MouseButton::Left)
        return;

    mListPressedIndex = (*sender->getUserData<std::pair<ItemModel::ModelIndex, ItemModel*> >()).first;
    mListDragStartX = left;
    mListDragStartY = top;
    mListDragStarted = false;
    forceItemFocused(mListPressedIndex);
}

void ItemView::onListItemDragged(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
{
    if (id != MyGUI::MouseButton::Left || mListDragStarted || mListPressedIndex < 0)
        return;

    const ItemModel::ModelIndex index
        = (*sender->getUserData<std::pair<ItemModel::ModelIndex, ItemModel*> >()).first;
    if (index != mListPressedIndex)
        return;

    constexpr int dragThreshold = 5;
    if (std::abs(left - mListDragStartX) < dragThreshold
        && std::abs(top - mListDragStartY) < dragThreshold)
        return;

    // Mark first: the delegate can start DragAndDrop immediately.
    mListDragStarted = true;
    eventItemDragStarted(index);
}

ItemView* ItemView::findVisibleItemViewAt(const MyGUI::IntPoint& point)
{
    // Iterate backwards so the most recently created/top-level item views win
    // when two windows overlap. Only the actual ItemView rectangle is a drop
    // target; releasing over unrelated controls keeps the floating item active.
    for (auto it = sExtendedItemViews.rbegin(); it != sExtendedItemViews.rend(); ++it)
    {
        ItemView* view = *it;
        if (!view || !view->mExtendedMode || !view->getVisible() || !view->getInheritedVisible())
            continue;
        if (view->getAbsoluteCoord().inside(point))
            return view;
    }
    return nullptr;
}

void ItemView::onListItemReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
{
    (void)left;
    (void)top;
    if (id != MyGUI::MouseButton::Left)
        return;

    if (!mListDragStarted)
    {
        const ItemModel::ModelIndex index
            = (*sender->getUserData<std::pair<ItemModel::ModelIndex, ItemModel*> >()).first;
        const bool sameItem = index == mListPressedIndex;
        mListPressedIndex = -1;

        // Barter/container windows explicitly opt into modern one-click
        // transfer. Regular inventory list rows still wait for double-click,
        // preserving equip/use behaviour outside two-pane transfer modes.
        if (mSingleClickActionEnabled && sameItem)
            eventItemClicked(index);
        return;
    }

    mListDragStarted = false;
    mListPressedIndex = -1;

    ItemView* target = findVisibleItemViewAt(MyGUI::InputManager::getInstance().getMousePosition());
    if (target)
        target->eventBackgroundClicked();
}

void ItemView::onListItemDoubleClicked(MyGUI::Widget* sender)
{
    if (mListDragStarted)
        return;

    const ItemModel::ModelIndex index
        = (*sender->getUserData<std::pair<ItemModel::ModelIndex, ItemModel*> >()).first;
    mListPressedIndex = index;
    forceItemFocused(index);
    eventItemDoubleClicked(index);
}

void ItemView::onSelectedBackground(MyGUI::Widget *sender)
{
    (void)sender;
    eventBackgroundClicked();
}

void ItemView::onMouseWheelMoved(MyGUI::Widget *_sender, int _rel)
{
    (void)_sender;
    MyGUI::ScrollView* view = getActiveScrollView();
    MyGUI::IntPoint offset = view->getViewOffset();
    if (mExtendedMode && mViewMode == View_List)
    {
        offset.top = std::min(0, static_cast<int>(offset.top + _rel * 0.3f));
    }
    else
    {
        offset.left = std::min(0, static_cast<int>(offset.left + _rel * 0.3f));
    }
    view->setViewOffset(offset);
}

void ItemView::onToggleView(MyGUI::Widget*)
{
    setViewMode(mViewMode == View_List ? View_Grid : View_List);
}

void ItemView::onSortDefault(MyGUI::Widget*)
{
    if (SortFilterItemModel* sort = dynamic_cast<SortFilterItemModel*>(mModel))
    {
        sort->setSortMode(SortFilterItemModel::Sort_Default, true);
        update();
    }
}

void ItemView::onSortName(MyGUI::Widget*)
{
    if (SortFilterItemModel* sort = dynamic_cast<SortFilterItemModel*>(mModel))
    {
        sort->toggleSortMode(SortFilterItemModel::Sort_Name);
        update();
    }
}

void ItemView::onSortCount(MyGUI::Widget*)
{
    if (SortFilterItemModel* sort = dynamic_cast<SortFilterItemModel*>(mModel))
    {
        sort->toggleSortMode(SortFilterItemModel::Sort_Count);
        update();
    }
}

void ItemView::onSortWeight(MyGUI::Widget*)
{
    if (SortFilterItemModel* sort = dynamic_cast<SortFilterItemModel*>(mModel))
    {
        sort->toggleSortMode(SortFilterItemModel::Sort_Weight);
        update();
    }
}

void ItemView::onSortValue(MyGUI::Widget*)
{
    if (SortFilterItemModel* sort = dynamic_cast<SortFilterItemModel*>(mModel))
    {
        sort->toggleSortMode(SortFilterItemModel::Sort_Value);
        update();
    }
}

void ItemView::setSize(const MyGUI::IntSize &_value)
{
    bool changed = (_value.width != getWidth() || _value.height != getHeight());
    Base::setSize(_value);
    if (changed)
    {
        updateExtendedGeometry();
        layoutWidgets();
    }
}

void ItemView::setCoord(const MyGUI::IntCoord &_value)
{
    bool changed = (_value.width != getWidth() || _value.height != getHeight());
    Base::setCoord(_value);
    if (changed)
    {
        updateExtendedGeometry();
        layoutWidgets();
    }
}

void ItemView::registerComponents()
{
    MyGUI::FactoryManager::getInstance().registerFactory<MWGui::ItemView>("Widget");
}

}
