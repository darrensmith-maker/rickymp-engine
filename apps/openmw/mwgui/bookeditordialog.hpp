#ifndef MWGUI_BOOKEDITORDIALOG_H
#define MWGUI_BOOKEDITORDIALOG_H

#include "windowbase.hpp"

#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

#include "../mwworld/ptr.hpp"

namespace MyGUI
{
    class Button;
    class EditBox;
    class TextBox;
    class Widget;
}

namespace ESM
{
    struct Book;
}

namespace MWGui
{
    /// Native ArenaMW writing/copying UI inspired by Scribo.
    ///
    /// New writing is started by using misc_quill. Existing vanilla books are
    /// copied, while books created by this dialog can be edited in-place.
    class BookEditorDialog : public WindowModal
    {
    public:
        BookEditorDialog();

        void openNew();
        void openForBook(const MWWorld::Ptr& source);
        bool exit() override;
        void onOpen() override;
        void onResChange(int, int) override { center(); }

        static bool isArenaWrittenBook(const MWWorld::Ptr& ptr);

    private:
        enum Mode
        {
            Mode_New,
            Mode_Copy,
            Mode_Edit
        };

        void reset();
        void refresh();
        void refreshCounter();
        void refreshMaterials();
        void refreshFormatButtons();

        void onSaveClicked(MyGUI::Widget* sender);
        void onCancelClicked(MyGUI::Widget* sender);
        void onBookFormatClicked(MyGUI::Widget* sender);
        void onScrollFormatClicked(MyGUI::Widget* sender);
        void onTextChanged(MyGUI::EditBox* sender);
        void onKeyPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);

        bool save();
        bool hasRequiredMaterials(std::string& missing) const;
        void consumeMaterials();
        int getPaperCount() const;
        int getRequiredPaperCount() const;

        const ESM::Book* findTemplate(bool scroll) const;
        std::string makeMarkup(const std::string& text) const;
        std::string makePlainText(const std::string& markup) const;
        std::string tr(const std::string& key) const;
        std::string trFormat(const std::string& key, const std::string& value) const;

        MyGUI::TextBox* mHeader;
        MyGUI::TextBox* mModeLabel;
        MyGUI::TextBox* mTitleLabel;
        MyGUI::EditBox* mTitleEdit;
        MyGUI::TextBox* mFormatLabel;
        MyGUI::Button* mBookFormatButton;
        MyGUI::Button* mScrollFormatButton;
        MyGUI::TextBox* mTextLabel;
        MyGUI::EditBox* mTextEdit;
        MyGUI::TextBox* mCounter;
        MyGUI::TextBox* mMaterials;
        MyGUI::TextBox* mHint;
        MyGUI::Button* mSaveButton;
        MyGUI::Button* mCancelButton;

        Mode mMode;
        bool mIsScroll;
        MWWorld::Ptr mSource;
        std::string mSourceMarkup;
        std::string mInitialPlainText;
    };
}

#endif
