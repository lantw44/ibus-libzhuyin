/* vim:set et ts=4 sts=4:
 *
 * ibus-libzhuyin - New Zhuyin engine based on libzhuyin for IBus
 *
 * Copyright (c) 2014 Peng Wu <alexepico@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "ZYZPhoneticEditor.h"
#include <assert.h>
#include "ZYConfig.h"
#include "ZYZhuyinProperties.h"
#include "ZYZPhoneticSection.h"
#include "ZYZBuiltinSymbolSection.h"
#include "ZYEnhancedText.h"
#include "ZYLibZhuyin.h"

namespace ZY {

/**
 * Implementation Notes:
 * 1. support input editing;
 * 2. support phonetic candidates;
 * 3. support built-in symbols candidates;
 * 4. support list all user symbols;
 * 5. support show user symbols;
 * 6. support easy symbols input;
 */

/* init static members */
PhoneticEditor::PhoneticEditor (ZhuyinProperties & props, Config & config)
    : EnhancedEditor (props, config),
      m_lookup_table (m_config.pageSize ())
{
    /* alloc one instance. */
    m_instance = LibZhuyinBackEnd::instance ().allocZhuyinInstance ();
    assert (NULL != m_instance);

    /* init symbols sections here. */
    m_symbol_sections[STATE_BUILTIN_SYMBOL_SHOWN].reset
        (new BuiltinSymbolSection (*this, props));

    m_phonetic_section.reset
        (new PhoneticSection (*this, props));
}

PhoneticEditor::~PhoneticEditor (void)
{
    m_text = "";
    m_cursor = 0;

    /* free m_instances */
    resizeInstances ();

    LibZhuyinBackEnd::instance ().freeZhuyinInstance (m_instance);
}

gboolean
PhoneticEditor::processEnter (guint keyval, guint keycode,
                              guint modifiers)
{
    if (!m_text)
        return FALSE;
    if (cmshm_filter (modifiers) != 0)
        return TRUE;

    commit ();
    return TRUE;
}

gboolean
PhoneticEditor::processFunctionKey (guint keyval, guint keycode,
                                    guint modifiers)
{
    if (m_text.empty ())
        return FALSE;

    /* ignore numlock */
    modifiers = cmshm_filter (modifiers);

    if (modifiers != 0)
        return TRUE;

    /* process some cursor control keys */
    if (modifiers == 0) { /* no modifiers. */
        switch (keyval) {
        case IBUS_BackSpace:
            removeCharBefore ();
            return TRUE;

        case IBUS_Delete:
        case IBUS_KP_Delete:
            removeCharAfter ();
            return TRUE;

        case IBUS_Left:
        case IBUS_KP_Left:
            moveCursorLeft ();
            return TRUE;

        case IBUS_Right:
        case IBUS_KP_Right:
            moveCursorRight ();
            return TRUE;

        case IBUS_Home:
        case IBUS_KP_Home:
            moveCursorToBegin ();
            return TRUE;

        case IBUS_End:
        case IBUS_KP_Enter:
            moveCursorToEnd ();
            return TRUE;

        case IBUS_Escape:
            reset ();
            return TRUE;

        default:
            return TRUE;
        }
    }

    return TRUE;
}

gboolean
PhoneticEditor::processCandidateKey (guint keyval, guint keycode,
                                     guint modifiers)
{
    if (!m_lookup_table.size ())
        return FALSE;

    /* ignore numlock */
    modifiers = cmshm_filter (modifiers);

    if (modifiers != 0)
        return TRUE;

    /* process some cursor control keys */
    if (modifiers == 0) { /* no modifiers. */
        switch (keyval) {
        case IBUS_Up:
        case IBUS_KP_Up:
            cursorUp ();
            return TRUE;

        case IBUS_Down:
        case IBUS_KP_Down:
            cursorDown ();
            return TRUE;

        case IBUS_Page_Up:
        case IBUS_KP_Page_Up:
            pageUp ();
            return TRUE;

        case IBUS_Page_Down:
        case IBUS_KP_Page_Down:
            pageDown ();
            return TRUE;

        default:
            break;
        }

        /* process candidate keys */
        std::string keys = m_config.candidateKeys ();
        std::size_t found = keys.find (keyval);
        if (found != std::string::npos) { /* found. */
            selectCandidateInPage (found);
        }
        return TRUE;
    }

    return TRUE;
}

gboolean
PhoneticEditor::processKeyEvent (guint keyval, guint keycode,
                                 guint modifiers)
{
    return FALSE;
}

void
PhoneticEditor::updateLookupTableFast (void)
{
    Editor::updateLookupTableFast (m_lookup_table, TRUE);
}

void
PhoneticEditor::updateLookupTable (void)
{
    m_lookup_table.clear ();

    fillLookupTableByPage ();
    if (m_lookup_table.size ()) {
        Editor::updateLookupTable (m_lookup_table, TRUE);
    } else {
        hideLookupTable ();
    }
}

gboolean
PhoneticEditor::fillLookupTableByPage (void)
{
    if (STATE_CANDIDATE_SHOWN == m_input_state)
        return m_phonetic_section->fillLookupTableByPage ();

    if (STATE_BUILTIN_SYMBOL_SHOWN == m_input_state /* ||
        STATE_USER_SYMBOL_LIST_ALL == m_input_state ||
        STATE_USER_SYMBOL_SHOWN == m_input_state */) {
        return m_symbol_sections[m_input_state]->
            fillLookupTableByPage ();
    }

    return FALSE;
}

void
PhoneticEditor::pageUp (void)
{
    if (G_LIKELY (m_lookup_table.pageUp ())) {
        updateLookupTableFast ();
        updatePreeditText ();
        updateAuxiliaryText ();
    }
}

void
PhoneticEditor::pageDown (void)
{
    if (G_LIKELY ((m_lookup_table.pageDown ()) ||
                  (fillLookupTableByPage () && m_lookup_table.pageDown ()))) {
        updateLookupTableFast ();
        updatePreeditText ();
        updateAuxiliaryText ();
    }
}

void
PhoneticEditor::cursorUp (void)
{
    if (G_LIKELY (m_lookup_table.cursorUp ())) {
        updateLookupTableFast ();
        updatePreeditText ();
        updateAuxiliaryText ();
    }
}

void
PhoneticEditor::cursorDown (void)
{
    if (G_LIKELY ((m_lookup_table.cursorPos () == m_lookup_table.size() - 1) &&
                  (fillLookupTableByPage () == FALSE))) {
        return;
    }

    if (G_LIKELY (m_lookup_table.cursorDown ())) {
        updateLookupTableFast ();
        updatePreeditText ();
        updateAuxiliaryText ();
    }
}

void
PhoneticEditor::candidateClicked (guint index, guint button,
                                  guint state)
{
    selectCandidateInPage (index);
}

void
PhoneticEditor::reset (void)
{
    m_lookup_table.clear ();
    m_buffer = "";

    zhuyin_reset (m_instance);

    zhuyin_instance_vec::iterator iter;
    for (; iter != m_instances.end (); ++iter) {
        LibZhuyinBackEnd::instance ().freeZhuyinInstance (*iter);
    }
    m_instances.clear ();

    EnhancedEditor::reset ();
}

void
PhoneticEditor::update (void)
{
    updateLookupTable ();
    updatePreeditText ();
    updateAuxiliaryText ();
}

void
PhoneticEditor::commit (const gchar *str)
{
    StaticText text(str);
    commitText (text);
}

gboolean
PhoneticEditor::selectCandidate (guint index)
{
    if (STATE_CANDIDATE_SHOWN == m_input_state)
        return m_phonetic_section->selectCandidate (index);

    if (STATE_BUILTIN_SYMBOL_SHOWN == m_input_state /* ||
        STATE_USER_SYMBOL_LIST_ALL == m_input_state ||
        STATE_USER_SYMBOL_SHOWN == m_input_state */) {
        return m_symbol_sections[m_input_state]->
            selectCandidate (index);
    }

    return FALSE;
}

gboolean
PhoneticEditor::selectCandidateInPage (guint index)
{
    guint page_size = m_lookup_table.pageSize ();
    guint cursor_pos = m_lookup_table.cursorPos ();

    if (G_UNLIKELY (index >= page_size))
        return FALSE;
    index += (cursor_pos / page_size) * page_size;

    return selectCandidate (index);
}

gboolean
PhoneticEditor::removeCharBefore (void)
{
    if (G_UNLIKELY (m_cursor == 0))
        return FALSE;

    m_cursor --;
    erase_input_sequence (m_text, m_cursor, 1);

    updateZhuyin ();
    update ();

    return TRUE;
}

gboolean
PhoneticEditor::removeCharAfter (void)
{
    if (G_UNLIKELY (m_cursor ==
                    get_enhanced_text_length (m_text)))
        return FALSE;

    erase_input_sequence (m_text, m_cursor, 1);

    updateZhuyin ();
    update ();

    return TRUE;
}

gboolean
PhoneticEditor::moveCursorLeft (void)
{
    if (G_UNLIKELY (m_cursor == 0))
        return FALSE;

    m_cursor --;
    update ();
    return TRUE;
}

gboolean
PhoneticEditor::moveCursorRight (void)
{
    if (G_UNLIKELY (m_cursor ==
                    get_enhanced_text_length (m_text)))
        return FALSE;

    m_cursor ++;
    update ();
    return TRUE;
}

gboolean
PhoneticEditor::moveCursorToBegin (void)
{
    if (G_UNLIKELY (m_cursor == 0))
        return FALSE;

    m_cursor = 0;
    update ();
    return TRUE;
}

gboolean
PhoneticEditor::moveCursorToEnd (void)
{
    if (G_UNLIKELY (m_cursor ==
                    get_enhanced_text_length (m_text)))
        return FALSE;

    m_cursor = get_enhanced_text_length (m_text);
    update ();
    return TRUE;
}

void
PhoneticEditor::resizeInstances (void)
{
    size_t num = get_number_of_phonetic_sections (m_text);

    /* re-allocate the zhuyin instances */
    if (num > m_instances.size ()) { /* need more instances. */
        for (size_t i = m_instances.size (); i < num; ++i) {
            /* allocate one instance */
            zhuyin_instance_t * instance = LibZhuyinBackEnd::instance ().
                allocZhuyinInstance ();
            assert (NULL != instance);
            m_instances.push_back (instance);
        }
    }

    if (num < m_instances.size ()) { /* free some instances. */
        for (size_t i = num; i < m_instances.size (); ++i) {
            LibZhuyinBackEnd::instance ().freeZhuyinInstance (m_instances[i]);
            m_instances[i] = NULL;
        }
        m_instances.resize (num);
    }
}


};
