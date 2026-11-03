#include "list.hpp"

#include <MyGUI_Gui.h>
#include <MyGUI_Button.h>
#include <MyGUI_ImageBox.h>

#include <algorithm>
#include <cmath>

namespace Gui
{

    MWList::MWList()
        : mScrollView(nullptr)
        , mClient(nullptr)
        , mItemHeight(0)
        , mItemFontHeight(0)
        , mItemMinHeight(0)
        , mItemSpacing(3)
        , mSelectedIndex(-1)
        , mWasDragged(false)
    {
    }

    void MWList::initialiseOverride()
    {
        Base::initialiseOverride();

        assignWidget(mClient, "Client");
        if (mClient == nullptr)
            mClient = this;

        mScrollView = mClient->createWidgetReal<MyGUI::ScrollView>(
            "MW_ScrollView", MyGUI::FloatCoord(0.0, 0.0, 1.0, 1.0),
            MyGUI::Align::Top | MyGUI::Align::Left | MyGUI::Align::Stretch, getName() + "_ScrollView");
        mScrollView->eventMouseButtonPressed += MyGUI::newDelegate(this, &MWList::onDragStart);
        mScrollView->eventMouseDrag += MyGUI::newDelegate(this, &MWList::onMouseDrag);
        mScrollView->eventMouseWheel += MyGUI::newDelegate(this, &MWList::onMouseWheelMoved);
    }

    void MWList::addItem(const std::string& name)
    {
        mItems.push_back(name);
    }

    void MWList::addSeparator()
    {
        mItems.emplace_back("");
    }

    void MWList::adjustSize()
    {
        redraw();
    }

    void MWList::redraw(bool scrollbarShown)
    {
        const int _scrollBarWidth = 20; // fetch this from skin?
        const int scrollBarWidth = scrollbarShown ? _scrollBarWidth : 0;
        int viewPosition = -mScrollView->getViewOffset().top;

        while (mScrollView->getChildCount())
        {
            MyGUI::Gui::getInstance().destroyWidget(mScrollView->getChildAt(0));
        }

        mItemWidgets.assign(mItems.size(), nullptr);
        mItemHeight = 0;
        int i=0;
        for (std::vector<std::string>::const_iterator it=mItems.begin();
            it!=mItems.end(); ++it)
        {
            if (*it != "")
            {
                if (mListItemSkin.empty())
                    return;
                MyGUI::Button* button = mScrollView->createWidget<MyGUI::Button>(
                    mListItemSkin, MyGUI::IntCoord(0, mItemHeight, mScrollView->getSize().width - scrollBarWidth - 2, 24),
                    MyGUI::Align::Left | MyGUI::Align::Top, getName() + "_item_" + std::to_string(i));
                if (mItemFontHeight > 0)
                    button->setProperty("FontHeight", std::to_string(mItemFontHeight));
                button->getSubWidgetText()->setWordWrap(true);
                button->getSubWidgetText()->setTextAlign(MyGUI::Align::Left);

                // Measure both captions: a selection marker can wrap a line
                // that fits without the marker and otherwise clip the answer.
                button->setCaption(*it);
                int height = button->getTextSize().height;
                if (!mSelectedPrefix.empty())
                {
                    button->setCaption(mSelectedPrefix + *it);
                    height = std::max(height, button->getTextSize().height);
                }
                button->setCaption(i == mSelectedIndex && !mSelectedPrefix.empty()
                    ? mSelectedPrefix + *it : *it);
                button->eventMouseWheel += MyGUI::newDelegate(this, &MWList::onMouseWheelMoved);
                button->eventMouseButtonClick += MyGUI::newDelegate(this, &MWList::onItemSelected);
                button->eventMouseButtonPressed += MyGUI::newDelegate(this, &MWList::onDragStart);
                button->eventMouseDrag += MyGUI::newDelegate(this, &MWList::onMouseDrag);
                button->setNeedKeyFocus(true);

                if (mItemMinHeight > 0)
                    height = std::max(height, mItemMinHeight);
                // Fractional UI scaling may round the measured glyph height
                // down, so leave a small safety margin for the last line.
                height += 4;
                button->setSize(MyGUI::IntSize(button->getSize().width, height));
                button->setUserData(i);
                button->setStateSelected(i == mSelectedIndex);
                mItemWidgets[static_cast<std::size_t>(i)] = button;

                mItemHeight += height + mItemSpacing;
            }
            else
            {
                MyGUI::ImageBox* separator = mScrollView->createWidget<MyGUI::ImageBox>("MW_HLine",
                    MyGUI::IntCoord(2, mItemHeight, mScrollView->getWidth() - scrollBarWidth - 4, 18),
                    MyGUI::Align::Left | MyGUI::Align::Top | MyGUI::Align::HStretch);
                separator->setNeedMouseFocus(false);

                mItemHeight += 18 + mItemSpacing;
            }
            ++i;
        }

        // Canvas size must be expressed with VScroll disabled, otherwise MyGUI would expand the scroll area when the scrollbar is hidden
        mScrollView->setVisibleVScroll(false);
        mScrollView->setCanvasSize(mClient->getSize().width, std::max(mItemHeight, mClient->getSize().height));
        mScrollView->setVisibleVScroll(true);

        if (!scrollbarShown && mItemHeight > mClient->getSize().height)
            redraw(true);

        int viewRange = mScrollView->getCanvasSize().height;
        if(viewPosition > viewRange)
            viewPosition = viewRange;
        mScrollView->setViewOffset(MyGUI::IntPoint(0, -viewPosition));
    }

    void MWList::setPropertyOverride(const std::string &_key, const std::string &_value)
    {
        if (_key == "ListItemSkin")
            mListItemSkin = _value;
        else if (_key == "ItemFontHeight")
            mItemFontHeight = MyGUI::utility::parseValue<int>(_value);
        else if (_key == "ItemMinHeight")
            mItemMinHeight = MyGUI::utility::parseValue<int>(_value);
        else if (_key == "ItemSpacing")
            mItemSpacing = std::max(0, MyGUI::utility::parseValue<int>(_value));
        else if (_key == "SelectedPrefix")
            mSelectedPrefix = _value;
        else
            Base::setPropertyOverride(_key, _value);
    }

    unsigned int MWList::getItemCount()
    {
        return static_cast<unsigned int>(mItems.size());
    }

    std::string MWList::getItemNameAt(unsigned int at)
    {
        assert(at < mItems.size() && "List item out of bounds");
        return mItems[at];
    }

    void MWList::removeItem(const std::string& name)
    {
        const auto found = std::find(mItems.begin(), mItems.end(), name);
        assert(found != mItems.end());
        if (found == mItems.end())
            return;

        const int removedIndex = static_cast<int>(std::distance(mItems.begin(), found));
        mItems.erase(found);
        if (mSelectedIndex == removedIndex)
            mSelectedIndex = -1;
        else if (mSelectedIndex > removedIndex)
            --mSelectedIndex;
    }

    void MWList::clear()
    {
        mItems.clear();
        mItemWidgets.clear();
        mSelectedIndex = -1;
    }

    void MWList::setVerticalViewOffset(int offset)
    {
        const int minOffset = std::min(0, mScrollView->getHeight() - mScrollView->getCanvasSize().height);
        offset = std::max(minOffset, std::min(0, offset));
        mScrollView->setViewOffset(MyGUI::IntPoint(0, offset));
    }

    void MWList::onMouseWheelMoved(MyGUI::Widget* _sender, int _rel)
    {
        setVerticalViewOffset(mScrollView->getViewOffset().top + static_cast<int>(_rel * 0.3f));
    }

    void MWList::onDragStart(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;

        mDragStart = MyGUI::IntPoint(left, top);
        mLastDragPosition = mDragStart;
        mWasDragged = false;
    }

    void MWList::onMouseDrag(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;

        const MyGUI::IntPoint current(left, top);
        const MyGUI::IntPoint total = current - mDragStart;
        if (std::abs(total.left) > 4 || std::abs(total.top) > 4)
            mWasDragged = true;

        const MyGUI::IntPoint difference = current - mLastDragPosition;
        if (mWasDragged)
            setVerticalViewOffset(mScrollView->getViewOffset().top + difference.top);
        mLastDragPosition = current;
    }

    void MWList::onItemSelected(MyGUI::Widget* _sender)
    {
        if (mWasDragged)
        {
            mWasDragged = false;
            return;
        }

        int id = *_sender->getUserData<int>();
        if (id < 0 || static_cast<std::size_t>(id) >= mItems.size())
            return;
        setSelectedIndex(id, false);
        eventItemSelected(mItems[static_cast<std::size_t>(id)], id);
        eventWidgetSelected(_sender);
    }

    void MWList::ensureItemVisible(int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= mItemWidgets.size())
            return;
        MyGUI::Button* button = mItemWidgets[static_cast<std::size_t>(index)];
        if (!button)
            return;

        const int viewTop = -mScrollView->getViewOffset().top;
        const int viewBottom = viewTop + mScrollView->getHeight();
        const int itemTop = button->getTop();
        const int itemBottom = itemTop + button->getHeight();
        if (itemTop < viewTop)
            setVerticalViewOffset(-itemTop);
        else if (itemBottom > viewBottom)
            setVerticalViewOffset(-(itemBottom - mScrollView->getHeight()));
    }

    void MWList::updateItemCaption(int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= mItems.size()
            || static_cast<std::size_t>(index) >= mItemWidgets.size())
            return;
        MyGUI::Button* button = mItemWidgets[static_cast<std::size_t>(index)];
        if (!button || mItems[static_cast<std::size_t>(index)].empty())
            return;
        button->setCaption(index == mSelectedIndex && !mSelectedPrefix.empty()
            ? mSelectedPrefix + mItems[static_cast<std::size_t>(index)]
            : mItems[static_cast<std::size_t>(index)]);
    }

    bool MWList::setSelectedIndex(int index, bool ensureVisible)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= mItems.size() || mItems[static_cast<std::size_t>(index)].empty())
            return false;

        const int oldIndex = mSelectedIndex;
        if (oldIndex >= 0 && static_cast<std::size_t>(oldIndex) < mItemWidgets.size())
        {
            if (MyGUI::Button* old = mItemWidgets[static_cast<std::size_t>(oldIndex)])
                old->setStateSelected(false);
        }

        mSelectedIndex = index;
        updateItemCaption(oldIndex);
        updateItemCaption(mSelectedIndex);
        if (static_cast<std::size_t>(mSelectedIndex) < mItemWidgets.size())
        {
            if (MyGUI::Button* current = mItemWidgets[static_cast<std::size_t>(mSelectedIndex)])
                current->setStateSelected(true);
        }
        if (ensureVisible)
            ensureItemVisible(mSelectedIndex);
        return true;
    }

    bool MWList::selectNext(int direction, bool wrap)
    {
        if (direction == 0 || mItems.empty())
            return false;
        direction = direction > 0 ? 1 : -1;

        int index = mSelectedIndex;
        if (index < 0)
            index = direction > 0 ? -1 : static_cast<int>(mItems.size());

        for (std::size_t attempt = 0; attempt < mItems.size(); ++attempt)
        {
            index += direction;
            if (index < 0 || index >= static_cast<int>(mItems.size()))
            {
                if (!wrap)
                    return false;
                index = direction > 0 ? 0 : static_cast<int>(mItems.size()) - 1;
            }
            if (!mItems[static_cast<std::size_t>(index)].empty())
                return setSelectedIndex(index, true);
        }
        return false;
    }

    bool MWList::activateSelected()
    {
        if (mSelectedIndex < 0 || static_cast<std::size_t>(mSelectedIndex) >= mItems.size()
            || mItems[static_cast<std::size_t>(mSelectedIndex)].empty())
            return false;

        MyGUI::Widget* widget = nullptr;
        if (static_cast<std::size_t>(mSelectedIndex) < mItemWidgets.size())
            widget = mItemWidgets[static_cast<std::size_t>(mSelectedIndex)];
        eventItemSelected(mItems[static_cast<std::size_t>(mSelectedIndex)], mSelectedIndex);
        if (widget)
            eventWidgetSelected(widget);
        return true;
    }

    void MWList::clearSelection()
    {
        const int oldIndex = mSelectedIndex;
        if (oldIndex >= 0 && static_cast<std::size_t>(oldIndex) < mItemWidgets.size())
        {
            if (MyGUI::Button* old = mItemWidgets[static_cast<std::size_t>(oldIndex)])
                old->setStateSelected(false);
        }
        mSelectedIndex = -1;
        updateItemCaption(oldIndex);
    }

    MyGUI::Button *MWList::getItemWidget(const std::string& name)
    {
        for (std::size_t i = 0; i < mItems.size(); ++i)
        {
            if (mItems[i] == name && i < mItemWidgets.size())
                return mItemWidgets[i];
        }
        return nullptr;
    }

    void MWList::scrollToTop()
    {
        mScrollView->setViewOffset(MyGUI::IntPoint(0, 0));
    }
}
