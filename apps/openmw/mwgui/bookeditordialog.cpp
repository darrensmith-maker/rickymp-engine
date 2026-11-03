#include "bookeditordialog.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_TextBox.h>

#include <components/esm/loadbook.hpp>
#include <components/misc/stringops.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "inventorywindow.hpp"
#include "mode.hpp"

namespace
{
    constexpr int sWriterMarkerSkill = -2;
    constexpr std::size_t sTitleLimit = 80;
    constexpr std::size_t sBookTextLimit = 12000;
    constexpr std::size_t sScrollTextLimit = 3000;

    const char* sPaperIds[] = { "sc_paper plain", "text_paper_roll_01" };

    std::string replaceFirst(std::string value, const std::string& needle, const std::string& replacement)
    {
        const std::size_t pos = value.find(needle);
        if (pos != std::string::npos)
            value.replace(pos, needle.size(), replacement);
        return value;
    }

    void replaceAll(std::string& value, const std::string& needle, const std::string& replacement)
    {
        if (needle.empty())
            return;
        std::size_t pos = 0;
        while ((pos = value.find(needle, pos)) != std::string::npos)
        {
            value.replace(pos, needle.size(), replacement);
            pos += replacement.size();
        }
    }

    // Pelagiad/Journalbook Magic Cards intentionally covers Cyrillic but does
    // not contain several modern punctuation glyphs (notably bullet, single
    // guillemets and the right Russian guillemet).  Normalise those characters
    // to book-safe ASCII so authored/copied text never turns into empty boxes.
    std::string normaliseBookGlyphs(std::string value)
    {
        replaceAll(value, "\xC2\xA0", " ");      // non-breaking space
        replaceAll(value, "\xC2\xAB", "\""); // left guillemet
        replaceAll(value, "\xC2\xBB", "\""); // right guillemet
        replaceAll(value, "\xE2\x80\xA2", "*"); // bullet
        replaceAll(value, "\xE2\x80\x98", "'"); // left single quote
        replaceAll(value, "\xE2\x80\x99", "'"); // right single quote
        replaceAll(value, "\xE2\x80\x9C", "\""); // left double quote
        replaceAll(value, "\xE2\x80\x9D", "\""); // right double quote
        replaceAll(value, "\xE2\x80\x93", "-"); // en dash
        replaceAll(value, "\xE2\x80\x94", "-"); // em dash
        replaceAll(value, "\xE2\x80\xB9", "["); // single left angle quote
        replaceAll(value, "\xE2\x80\xBA", "]"); // single right angle quote
        return value;
    }

    std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
}

namespace MWGui
{
    BookEditorDialog::BookEditorDialog()
        : WindowModal("openmw_book_editor.layout")
        , mHeader(nullptr)
        , mModeLabel(nullptr)
        , mTitleLabel(nullptr)
        , mTitleEdit(nullptr)
        , mFormatLabel(nullptr)
        , mBookFormatButton(nullptr)
        , mScrollFormatButton(nullptr)
        , mTextLabel(nullptr)
        , mTextEdit(nullptr)
        , mCounter(nullptr)
        , mMaterials(nullptr)
        , mHint(nullptr)
        , mSaveButton(nullptr)
        , mCancelButton(nullptr)
        , mMode(Mode_New)
        , mIsScroll(false)
    {
        getWidget(mHeader, "Header");
        getWidget(mModeLabel, "ModeLabel");
        getWidget(mTitleLabel, "TitleLabel");
        getWidget(mTitleEdit, "TitleEdit");
        getWidget(mFormatLabel, "FormatLabel");
        getWidget(mBookFormatButton, "BookFormatButton");
        getWidget(mScrollFormatButton, "ScrollFormatButton");
        getWidget(mTextLabel, "TextLabel");
        getWidget(mTextEdit, "TextEdit");
        getWidget(mCounter, "Counter");
        getWidget(mMaterials, "Materials");
        getWidget(mHint, "Hint");
        getWidget(mSaveButton, "SaveButton");
        getWidget(mCancelButton, "CancelButton");

        mSaveButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookEditorDialog::onSaveClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookEditorDialog::onCancelClicked);
        mBookFormatButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookEditorDialog::onBookFormatClicked);
        mScrollFormatButton->eventMouseButtonClick += MyGUI::newDelegate(this, &BookEditorDialog::onScrollFormatClicked);
        mTitleEdit->eventEditTextChange += MyGUI::newDelegate(this, &BookEditorDialog::onTextChanged);
        mTextEdit->eventEditTextChange += MyGUI::newDelegate(this, &BookEditorDialog::onTextChanged);

        mSaveButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookEditorDialog::onKeyPressed);
        mCancelButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookEditorDialog::onKeyPressed);
        mTitleEdit->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookEditorDialog::onKeyPressed);
        mTextEdit->eventKeyButtonPressed += MyGUI::newDelegate(this, &BookEditorDialog::onKeyPressed);

        center();
    }

    std::string BookEditorDialog::tr(const std::string& key) const
    {
        return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
    }

    std::string BookEditorDialog::trFormat(const std::string& key, const std::string& value) const
    {
        return replaceFirst(tr(key), "%s", value);
    }

    bool BookEditorDialog::isArenaWrittenBook(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || ptr.getTypeName() != typeid(ESM::Book).name())
            return false;
        const MWWorld::LiveCellRef<ESM::Book>* ref = ptr.get<ESM::Book>();
        return ref->mBase->mData.mSkillId == sWriterMarkerSkill
            && ref->mBase->mEnchant.empty() && ref->mBase->mScript.empty();
    }

    void BookEditorDialog::reset()
    {
        mMode = Mode_New;
        mIsScroll = false;
        mSource = MWWorld::Ptr();
        mSourceMarkup.clear();
        mInitialPlainText.clear();
        mTitleEdit->setCaption("");
        mTextEdit->setCaption("");
        refresh();
    }

    void BookEditorDialog::openNew()
    {
        reset();
        mMode = Mode_New;
        mTitleEdit->setCaption(tr("writer.untitled_book"));
        refresh();
        setVisible(true);
    }

    void BookEditorDialog::openForBook(const MWWorld::Ptr& source)
    {
        if (source.isEmpty() || source.getTypeName() != typeid(ESM::Book).name())
            return;

        mSource = source;
        const MWWorld::LiveCellRef<ESM::Book>* ref = source.get<ESM::Book>();
        mIsScroll = ref->mBase->mData.mIsScroll != 0;
        mSourceMarkup = ref->mBase->mText;
        mInitialPlainText = makePlainText(mSourceMarkup);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        const bool inPlayerInventory = source.getContainerStore() == &player.getClass().getContainerStore(player);
        mMode = inPlayerInventory && isArenaWrittenBook(source) ? Mode_Edit : Mode_Copy;

        if (mMode == Mode_Copy)
            mTitleEdit->setCaption(trFormat("writer.copy_name", ref->mBase->mName));
        else
            mTitleEdit->setCaption(ref->mBase->mName);
        mTextEdit->setCaption(mInitialPlainText);

        refresh();
        setVisible(true);
    }

    void BookEditorDialog::onOpen()
    {
        WindowModal::onOpen();
        center();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTitleEdit);
    }

    bool BookEditorDialog::exit()
    {
        setVisible(false);
        return true;
    }

    void BookEditorDialog::refresh()
    {
        mHeader->setCaption(tr("writer.header"));
        mTitleLabel->setCaption(tr("writer.title"));
        mFormatLabel->setCaption(tr("writer.format"));
        mTextLabel->setCaption(tr("writer.text"));
        mBookFormatButton->setCaption(tr("writer.book"));
        mScrollFormatButton->setCaption(tr("writer.scroll"));
        mSaveButton->setCaption(tr("writer.save"));
        mCancelButton->setCaption(tr("writer.cancel"));
        mHint->setCaption(tr("writer.hint"));

        if (mMode == Mode_New)
            mModeLabel->setCaption(tr("writer.mode_new"));
        else if (mMode == Mode_Copy)
            mModeLabel->setCaption(tr("writer.mode_copy"));
        else
            mModeLabel->setCaption(tr("writer.mode_edit"));

        const bool formatEditable = mMode == Mode_New;
        mBookFormatButton->setEnabled(formatEditable);
        mScrollFormatButton->setEnabled(formatEditable);

        refreshFormatButtons();
        refreshCounter();
        refreshMaterials();
    }

    void BookEditorDialog::refreshFormatButtons()
    {
        mBookFormatButton->setStateSelected(!mIsScroll);
        mScrollFormatButton->setStateSelected(mIsScroll);
    }

    void BookEditorDialog::refreshCounter()
    {
        const std::size_t maxText = mIsScroll ? sScrollTextLimit : sBookTextLimit;
        const std::size_t current = mTextEdit->getCaption().size();
        std::ostringstream stream;
        stream << current << " / " << maxText;
        mCounter->setCaption(trFormat("writer.characters", stream.str()));

        const bool validTitle = !mTitleEdit->getCaption().empty() && mTitleEdit->getCaption().size() <= sTitleLimit;
        const bool validText = current <= maxText;
        std::string missing;
        mSaveButton->setEnabled(validTitle && validText && hasRequiredMaterials(missing));
    }

    int BookEditorDialog::getPaperCount() const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);
        int count = 0;
        for (const char* id : sPaperIds)
            count += store.count(id);
        return count;
    }

    int BookEditorDialog::getRequiredPaperCount() const
    {
        if (mMode == Mode_Edit)
            return 0;
        return mIsScroll ? 1 : 4;
    }

    bool BookEditorDialog::hasRequiredMaterials(std::string& missing) const
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);

        if (store.count("misc_quill") < 1)
        {
            missing = tr("writer.need_quill");
            return false;
        }
        if (store.count("misc_inkwell") < 1)
        {
            missing = tr("writer.need_ink");
            return false;
        }
        if (getPaperCount() < getRequiredPaperCount())
        {
            missing = trFormat("writer.need_paper", std::to_string(getRequiredPaperCount()));
            return false;
        }
        missing.clear();
        return true;
    }

    void BookEditorDialog::refreshMaterials()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);

        std::ostringstream stream;
        stream << tr("writer.materials") << "  "
               << tr("writer.quill") << ": " << (store.count("misc_quill") > 0 ? tr("writer.ok") : tr("writer.missing"))
               << "   " << tr("writer.ink") << ": " << (store.count("misc_inkwell") > 0 ? tr("writer.ok") : tr("writer.missing"));

        const int requiredPaper = getRequiredPaperCount();
        if (requiredPaper > 0)
            stream << "   " << tr("writer.paper") << ": " << getPaperCount() << "/" << requiredPaper;
        else
            stream << "   " << tr("writer.paper") << ": " << tr("writer.not_required");

        std::string status = stream.str();
        std::string missing;
        if (!hasRequiredMaterials(missing))
            status += "\n" + missing;
        mMaterials->setCaption(status);
    }

    void BookEditorDialog::consumeMaterials()
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);

        // The quill is a reusable tool. One inkwell is intentionally spent per
        // finished work; this keeps the native version predictable instead of
        // hiding a per-inkwell charge counter in custom state.
        store.remove("misc_inkwell", 1, player);

        int remaining = getRequiredPaperCount();
        for (const char* id : sPaperIds)
        {
            if (remaining <= 0)
                break;
            const int available = store.count(id);
            const int use = std::min(available, remaining);
            if (use > 0)
            {
                store.remove(id, use, player);
                remaining -= use;
            }
        }
    }

    const ESM::Book* BookEditorDialog::findTemplate(bool scroll) const
    {
        const MWWorld::ESMStore& store = MWBase::Environment::get().getWorld()->getStore();

        if (scroll)
        {
            if (const ESM::Book* note = store.get<ESM::Book>().search("bk_note"))
                return note;
        }

        const ESM::Book* best = nullptr;
        int bestScore = std::numeric_limits<int>::max();
        for (const ESM::Book& candidate : store.get<ESM::Book>())
        {
            if ((candidate.mData.mIsScroll != 0) != scroll)
                continue;
            if (candidate.mModel.empty() || candidate.mIcon.empty())
                continue;
            if (!candidate.mEnchant.empty() || !candidate.mScript.empty())
                continue;

            int score = std::max(0, candidate.mData.mValue);
            if (candidate.mData.mSkillId >= 0)
                score += 1000;
            if (candidate.mText.empty())
                score -= 100;
            if (score < bestScore)
            {
                best = &candidate;
                bestScore = score;
            }
        }
        return best;
    }

    std::string BookEditorDialog::makeMarkup(const std::string& text) const
    {
        const std::string safeText = normaliseBookGlyphs(text);
        std::string result;
        result.reserve(safeText.size() + 64);
        result += "<DIV ALIGN=\"justify\">";
        for (char c : safeText)
        {
            if (c == '\r')
                continue;
            if (c == '\n')
                result += "<BR>";
            // Literal angle brackets would be parsed as book markup.  The old
            // implementation substituted ‹/›, but Pelagiad has no glyphs for
            // those characters.  Square brackets are visually clear and safe.
            else if (c == '<')
                result += '[';
            else if (c == '>')
                result += ']';
            else
                result += c;
        }
        result += "</DIV><BR>";
        return result;
    }

    std::string BookEditorDialog::makePlainText(const std::string& markup) const
    {
        std::string result;
        result.reserve(markup.size());

        for (std::size_t i = 0; i < markup.size();)
        {
            if (markup[i] != '<')
            {
                result += markup[i++];
                continue;
            }

            const std::size_t end = markup.find('>', i + 1);
            if (end == std::string::npos)
            {
                result += markup.substr(i);
                break;
            }

            const std::string tag = lower(markup.substr(i + 1, end - i - 1));
            if (tag == "br" || tag.rfind("br ", 0) == 0)
                result += '\n';
            else if (tag == "p" || tag.rfind("p ", 0) == 0)
            {
                if (!result.empty() && result.back() != '\n')
                    result += '\n';
                result += '\n';
            }
            i = end + 1;
        }

        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return normaliseBookGlyphs(result);
    }

    bool BookEditorDialog::save()
    {
        if (MWMechanics::isPlayerInCombat())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{arenamp=writer.no_combat}");
            return false;
        }

        std::string missing;
        if (!hasRequiredMaterials(missing))
        {
            MWBase::Environment::get().getWindowManager()->messageBox(missing);
            refreshMaterials();
            return false;
        }

        const std::size_t titleCharacters = mTitleEdit->getCaption().size();
        const std::size_t textCharacters = mTextEdit->getCaption().size();
        const std::string title = normaliseBookGlyphs(mTitleEdit->getCaption());
        const std::string plainText = normaliseBookGlyphs(mTextEdit->getCaption());
        if (title.empty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{arenamp=writer.title_required}");
            return false;
        }

        const std::size_t maxText = mIsScroll ? sScrollTextLimit : sBookTextLimit;
        if (titleCharacters > sTitleLimit || textCharacters > maxText)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{arenamp=writer.too_long}");
            return false;
        }

        ESM::Book record;
        if (!mSource.isEmpty())
            record = *mSource.get<ESM::Book>()->mBase;
        else
        {
            const ESM::Book* base = findTemplate(mIsScroll);
            if (!base)
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{arenamp=writer.no_template}");
                return false;
            }
            record = *base;
        }

        record.mId.clear();
        record.mName = title;
        record.mData.mIsScroll = mIsScroll ? 1 : 0;
        record.mData.mSkillId = sWriterMarkerSkill;
        record.mData.mEnchant = 0;
        record.mEnchant.clear();
        record.mScript.clear();
        record.mData.mValue = std::max(1, mIsScroll ? 10 : 25);

        // Preserve original formatting when copying without changing the body.
        // Authored/edited text uses a safe plain-text-to-book-markup conversion.
        if (mMode == Mode_Copy && plainText == mInitialPlainText && !mSourceMarkup.empty()
            && normaliseBookGlyphs(mSourceMarkup) == mSourceMarkup)
            record.mText = mSourceMarkup;
        else
            record.mText = makeMarkup(plainText);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);

        // ArenaMP: the server is authoritative for authored books. Remove the
        // edited source/materials through the normal inventory packet path, then
        // send the complete BOOK dynamic record without a baseId. The server
        // assigns the final $custom id, persists the record in RecordStore, adds
        // it to the player's saved inventory and sends that authoritative record
        // back to the client. Never add a client-only final copy here: doing so
        // creates duplicate/unsynchronised books after reconnect.
        if (mMode == Mode_Edit && !mSource.isEmpty())
        {
            // Close the reader before invalidating its source Ptr.
            MWBase::Environment::get().getWindowManager()->removeGuiMode(mIsScroll ? GM_Scroll : GM_Book, true);
            store.remove(mSource, 1, player);
        }

        consumeMaterials();

        mwmp::Main::get().getNetworking()->getWorldstate()->sendBookRecord(&record);

        if (InventoryWindow* inventory = MWBase::Environment::get().getWindowManager()->getInventoryWindow())
            inventory->updateItemView();

        MWBase::Environment::get().getWindowManager()->playSound("book page2");
        MWBase::Environment::get().getWindowManager()->messageBox(
            mIsScroll ? "#{arenamp=writer.saved_scroll}" : "#{arenamp=writer.saved_book}");
        return true;
    }

    void BookEditorDialog::onSaveClicked(MyGUI::Widget*)
    {
        if (save())
            setVisible(false);
    }

    void BookEditorDialog::onCancelClicked(MyGUI::Widget*)
    {
        setVisible(false);
    }

    void BookEditorDialog::onBookFormatClicked(MyGUI::Widget*)
    {
        if (mMode != Mode_New)
            return;
        mIsScroll = false;
        if (mTitleEdit->getCaption() == tr("writer.untitled_scroll"))
            mTitleEdit->setCaption(tr("writer.untitled_book"));
        refresh();
    }

    void BookEditorDialog::onScrollFormatClicked(MyGUI::Widget*)
    {
        if (mMode != Mode_New)
            return;
        mIsScroll = true;
        if (mTitleEdit->getCaption() == tr("writer.untitled_book"))
            mTitleEdit->setCaption(tr("writer.untitled_scroll"));
        refresh();
    }

    void BookEditorDialog::onTextChanged(MyGUI::EditBox*)
    {
        refreshCounter();
        refreshMaterials();
    }

    void BookEditorDialog::onKeyPressed(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
    {
        if (key == MyGUI::KeyCode::Escape)
            setVisible(false);
        else if (key == MyGUI::KeyCode::Return && MyGUI::InputManager::getInstance().isControlPressed())
            onSaveClicked(nullptr);
    }
}
