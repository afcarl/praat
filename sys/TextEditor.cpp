/* TextEditor.cpp
 *
 * Copyright (C) 1997-2012,2013,2015 Paul Boersma, 2010 Franz Brausse
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "TextEditor.h"
#include "machine.h"
#include "longchar.h"
#include "EditorM.h"
#include "UnicodeData.h"

Thing_implement (TextEditor, Editor, 0);

#include "prefs_define.h"
#include "TextEditor_prefs.h"
#include "prefs_install.h"
#include "TextEditor_prefs.h"
#include "prefs_copyToInstance.h"
#include "TextEditor_prefs.h"

static Collection theOpenTextEditors;   // cannot be an autoCollection until Collection_undangleItem() isn't called in v_destroy()

/***** TextEditor methods *****/

void structTextEditor :: v_destroy () {
	if (theOpenTextEditors) {
		Collection_undangleItem (theOpenTextEditors, this);
	}
	TextEditor_Parent :: v_destroy ();
}

void structTextEditor :: v_nameChanged () {
	if (v_fileBased ()) {
		bool dirtinessAlreadyShown = GuiWindow_setDirty (our d_windowForm, our dirty);
		static MelderString windowTitle { 0 };
		if (our name [0] == U'\0') {
			MelderString_copy (& windowTitle, U"(untitled");
			if (dirty && ! dirtinessAlreadyShown)
				MelderString_append (& windowTitle, U", modified");
			MelderString_append (& windowTitle, U")");
		} else {
			MelderString_copy (& windowTitle, U"File ", MelderFile_messageName (& our file));
			if (dirty && ! dirtinessAlreadyShown)
				MelderString_append (& windowTitle, U" (modified)");
		}
		GuiShell_setTitle (our d_windowForm, windowTitle.string);
		//MelderString_copy (& windowTitle, our dirty && ! dirtinessAlreadyShown ? U"*" : U"", our name [0] == U'\0' ? U"(untitled)" : MelderFile_name (& our file));
	} else {
		TextEditor_Parent :: v_nameChanged ();
	}
}

static void openDocument (TextEditor me, MelderFile file) {
	if (theOpenTextEditors) {
		for (long ieditor = 1; ieditor <= theOpenTextEditors -> size; ieditor ++) {
			TextEditor editor = (TextEditor) theOpenTextEditors -> item [ieditor];
			if (editor != me && MelderFile_equal (file, & editor -> file)) {
				Editor_raise (editor);
				Melder_appendError (U"Text file ", file, U" is already open.");
				forget (me);   // don't forget me before Melder_appendError, because "file" is owned by one of my dialogs
				Melder_flushError ();
				return;
			}
		}
	}
	autostring32 text = MelderFile_readText (file);
	GuiText_setString (my textWidget, text.peek());
	/*
	 * GuiText_setString has invoked the changeCallback,
	 * which has set `my dirty` to `true`. Fix this.
	 */
	my dirty = false;
	MelderFile_copy (file, & my file);
	Thing_setName (me, Melder_fileToPath (file));
}

static void newDocument (TextEditor me) {
	GuiText_setString (my textWidget, U"");   // implicitly sets my dirty to `true`
	my dirty = false;
	if (my v_fileBased ()) Thing_setName (me, U"");
}

static void saveDocument (TextEditor me, MelderFile file) {
	autostring32 text = GuiText_getString (my textWidget);
	MelderFile_writeText (file, text.peek(), Melder_getOutputEncoding ());
	my dirty = false;
	MelderFile_copy (file, & my file);
	if (my v_fileBased ()) Thing_setName (me, Melder_fileToPath (file));
}

static void closeDocument (TextEditor me) {
	forget (me);
}

static void cb_open_ok (UiForm sendingForm, int /* narg */, Stackel /* args */, const char32 * /* sendingString */,
	Interpreter /* interpreter */, const char32 * /* invokingButtonTitle */, bool /* modified */, I)
{
	iam (TextEditor);
	MelderFile file = UiFile_getFile (sendingForm);
	openDocument (me, file);
}

static void cb_showOpen (EditorCommand cmd, UiForm /* sendingForm */, const char32 * /* sendingString */, Interpreter /* interpreter */) {
	TextEditor me = (TextEditor) cmd -> d_editor;
	if (! my openDialog)
		my openDialog = UiInfile_create (my d_windowForm, U"Open", cb_open_ok, me, nullptr, nullptr, false);
	UiInfile_do (my openDialog.get());
}

static void cb_saveAs_ok (UiForm sendingForm, int /* narg */, Stackel /* args */, const char32 * /* sendingString */,
	Interpreter /* interpreter */, const char32 * /* invokingButtonTitle */, bool /* modified */, I)
{
	iam (TextEditor);
	MelderFile file = UiFile_getFile (sendingForm);
	saveDocument (me, file);
}

static void menu_cb_saveAs (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	if (! my saveDialog)
		my saveDialog = UiOutfile_create (my d_windowForm, U"Save", cb_saveAs_ok, me, nullptr, nullptr);
	char32 defaultName [300];
	Melder_sprint (defaultName,300, ! my v_fileBased () ? U"info.txt" : my name [0] ? MelderFile_name (& my file) : U"");
	UiOutfile_do (my saveDialog.get(), defaultName);
}

static void gui_button_cb_saveAndOpen (I, GuiButtonEvent /* event */) {
	EditorCommand cmd = (EditorCommand) void_me;
	TextEditor me = (TextEditor) cmd -> d_editor;
	GuiThing_hide (my dirtyOpenDialog);
	if (my name [0]) {
		try {
			saveDocument (me, & my file);
		} catch (MelderError) {
			Melder_flushError ();
			return;
		}
		cb_showOpen (cmd, nullptr, nullptr, nullptr);
	} else {
		menu_cb_saveAs (me, cmd, nullptr, 0, nullptr, nullptr, nullptr);
	}
}

static void gui_button_cb_cancelOpen (I, GuiButtonEvent /* event */) {
	EditorCommand cmd = (EditorCommand) void_me;
	TextEditor me = (TextEditor) cmd -> d_editor;
	GuiThing_hide (my dirtyOpenDialog);
}

static void gui_button_cb_discardAndOpen (I, GuiButtonEvent /* event */) {
	EditorCommand cmd = (EditorCommand) void_me;
	TextEditor me = (TextEditor) cmd -> d_editor;
	GuiThing_hide (my dirtyOpenDialog);
	cb_showOpen (cmd, nullptr, nullptr, nullptr);
}

static void menu_cb_open (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	if (my dirty) {
		if (! my dirtyOpenDialog) {
			int buttonWidth = 120, buttonSpacing = 20;
			my dirtyOpenDialog = GuiDialog_create (my d_windowForm,
				150, 70,
				Gui_LEFT_DIALOG_SPACING + 3 * buttonWidth + 2 * buttonSpacing + Gui_RIGHT_DIALOG_SPACING,
				Gui_TOP_DIALOG_SPACING + Gui_TEXTFIELD_HEIGHT + Gui_VERTICAL_DIALOG_SPACING_SAME + 2 * Gui_BOTTOM_DIALOG_SPACING + Gui_PUSHBUTTON_HEIGHT,
				U"Text changed", nullptr, nullptr, GuiDialog_MODAL);
			GuiLabel_createShown (my dirtyOpenDialog,
				Gui_LEFT_DIALOG_SPACING, - Gui_RIGHT_DIALOG_SPACING,
				Gui_TOP_DIALOG_SPACING, Gui_TOP_DIALOG_SPACING + Gui_LABEL_HEIGHT,
				U"The text has changed! Save changes?", 0);
			int x = Gui_LEFT_DIALOG_SPACING, y = - Gui_BOTTOM_DIALOG_SPACING;
			GuiButton_createShown (my dirtyOpenDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Discard & Open", gui_button_cb_discardAndOpen, cmd, 0);
			x += buttonWidth + buttonSpacing;
			GuiButton_createShown (my dirtyOpenDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Cancel", gui_button_cb_cancelOpen, cmd, 0);
			x += buttonWidth + buttonSpacing;
			GuiButton_createShown (my dirtyOpenDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Save & Open", gui_button_cb_saveAndOpen, cmd, 0);
		}
		GuiThing_show (my dirtyOpenDialog);
	} else {
		cb_showOpen (cmd, sendingForm, sendingString, interpreter);
	}
}

static void gui_button_cb_saveAndNew (I, GuiButtonEvent /* event */) {
	EditorCommand cmd = (EditorCommand) void_me;
	TextEditor me = (TextEditor) cmd -> d_editor;
	GuiThing_hide (my dirtyNewDialog);
	if (my name [0]) {
		try {
			saveDocument (me, & my file);
		} catch (MelderError) {
			Melder_flushError ();
			return;
		}
		newDocument (me);
	} else {
		menu_cb_saveAs (me, cmd, nullptr, 0, nullptr, nullptr, nullptr);
	}
}

static void gui_button_cb_cancelNew (I, GuiButtonEvent /* event */) {
	EditorCommand cmd = (EditorCommand) void_me;
	TextEditor me = (TextEditor) cmd -> d_editor;
	GuiThing_hide (my dirtyNewDialog);
}

static void gui_button_cb_discardAndNew (I, GuiButtonEvent /* event */) {
	EditorCommand cmd = (EditorCommand) void_me;
	TextEditor me = (TextEditor) cmd -> d_editor;
	GuiThing_hide (my dirtyNewDialog);
	newDocument (me);
}

static void menu_cb_new (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	if (my v_fileBased () && my dirty) {
		if (! my dirtyNewDialog) {
			int buttonWidth = 120, buttonSpacing = 20;
			my dirtyNewDialog = GuiDialog_create (my d_windowForm,
				150, 70, Gui_LEFT_DIALOG_SPACING + 3 * buttonWidth + 2 * buttonSpacing + Gui_RIGHT_DIALOG_SPACING,
					Gui_TOP_DIALOG_SPACING + Gui_TEXTFIELD_HEIGHT + Gui_VERTICAL_DIALOG_SPACING_SAME + 2 * Gui_BOTTOM_DIALOG_SPACING + Gui_PUSHBUTTON_HEIGHT,
				U"Text changed", nullptr, nullptr, GuiDialog_MODAL);
			GuiLabel_createShown (my dirtyNewDialog,
				Gui_LEFT_DIALOG_SPACING, - Gui_RIGHT_DIALOG_SPACING,
				Gui_TOP_DIALOG_SPACING, Gui_TOP_DIALOG_SPACING + Gui_LABEL_HEIGHT,
				U"The text has changed! Save changes?", 0);
			int x = Gui_LEFT_DIALOG_SPACING, y = - Gui_BOTTOM_DIALOG_SPACING;
			GuiButton_createShown (my dirtyNewDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Discard & New", gui_button_cb_discardAndNew, cmd, 0);
			x += buttonWidth + buttonSpacing;
			GuiButton_createShown (my dirtyNewDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Cancel", gui_button_cb_cancelNew, cmd, 0);
			x += buttonWidth + buttonSpacing;
			GuiButton_createShown (my dirtyNewDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Save & New", gui_button_cb_saveAndNew, cmd, 0);
		}
		GuiThing_show (my dirtyNewDialog);
	} else {
		newDocument (me);
	}
}

static void menu_cb_clear (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	my v_clear ();
}

static void menu_cb_save (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	if (my name [0]) {
		try {
			saveDocument (me, & my file);
		} catch (MelderError) {
			Melder_flushError ();
			return;
		}
	} else {
		menu_cb_saveAs (me, cmd, nullptr, 0, nullptr, nullptr, nullptr);
	}
}

static void menu_cb_reopen (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	if (my name [0]) {
		try {
			openDocument (me, & my file);
		} catch (MelderError) {
			Melder_flushError ();
			return;
		}
	} else {
		Melder_throw (U"Cannot reopen from disk, because the text has never been saved yet.");
	}
}

static void gui_button_cb_saveAndClose (I, GuiButtonEvent /* event */) {
	iam (TextEditor);
	GuiThing_hide (my dirtyCloseDialog);
	if (my name [0]) {
		try {
			saveDocument (me, & my file);
		} catch (MelderError) {
			Melder_flushError ();
			return;
		}
		closeDocument (me);
	} else {
		menu_cb_saveAs (me, Editor_getMenuCommand (me, U"File", U"Save as..."), nullptr, 0, nullptr, nullptr, nullptr);
	}
}

static void gui_button_cb_cancelClose (I, GuiButtonEvent /* event */) {
	iam (TextEditor);
	GuiThing_hide (my dirtyCloseDialog);
}

static void gui_button_cb_discardAndClose (I, GuiButtonEvent /* event */) {
	iam (TextEditor);
	GuiThing_hide (my dirtyCloseDialog);
	closeDocument (me);
}

void structTextEditor :: v_goAway () {
	if (v_fileBased () && dirty) {
		if (! dirtyCloseDialog) {
			int buttonWidth = 120, buttonSpacing = 20;
			dirtyCloseDialog = GuiDialog_create (d_windowForm,
				150, 70, Gui_LEFT_DIALOG_SPACING + 3 * buttonWidth + 2 * buttonSpacing + Gui_RIGHT_DIALOG_SPACING,
					Gui_TOP_DIALOG_SPACING + Gui_TEXTFIELD_HEIGHT + Gui_VERTICAL_DIALOG_SPACING_SAME + 2 * Gui_BOTTOM_DIALOG_SPACING + Gui_PUSHBUTTON_HEIGHT,
				U"Text changed", nullptr, nullptr, GuiDialog_MODAL);
			GuiLabel_createShown (dirtyCloseDialog,
				Gui_LEFT_DIALOG_SPACING, - Gui_RIGHT_DIALOG_SPACING,
				Gui_TOP_DIALOG_SPACING, Gui_TOP_DIALOG_SPACING + Gui_LABEL_HEIGHT,
				U"The text has changed! Save changes?", 0);
			int x = Gui_LEFT_DIALOG_SPACING, y = - Gui_BOTTOM_DIALOG_SPACING;
			GuiButton_createShown (dirtyCloseDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Discard & Close", gui_button_cb_discardAndClose, this, 0);
			x += buttonWidth + buttonSpacing;
			GuiButton_createShown (dirtyCloseDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Cancel", gui_button_cb_cancelClose, this, 0);
			x += buttonWidth + buttonSpacing;
			GuiButton_createShown (dirtyCloseDialog,
				x, x + buttonWidth, y - Gui_PUSHBUTTON_HEIGHT, y,
				U"Save & Close", gui_button_cb_saveAndClose, this, 0);
		}
		GuiThing_show (dirtyCloseDialog);
	} else {
		closeDocument (this);
	}
}

static void menu_cb_undo (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	GuiText_undo (my textWidget);
}

static void menu_cb_redo (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	GuiText_redo (my textWidget);
}

static void menu_cb_cut (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	GuiText_cut (my textWidget);  // use ((XmAnyCallbackStruct *) call) -> event -> xbutton. time
}

static void menu_cb_copy (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	GuiText_copy (my textWidget);
}

static void menu_cb_paste (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	GuiText_paste (my textWidget);
}

static void menu_cb_erase (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	GuiText_remove (my textWidget);
}

static bool getSelectedLines (TextEditor me, long *firstLine, long *lastLine) {
	long left, right;
	char32 *text = GuiText_getStringAndSelectionPosition (my textWidget, & left, & right);
	long textLength = str32len (text);
	Melder_assert (left >= 0);
	Melder_assert (left <= right);
	Melder_assert (right <= textLength);
	long i = 0;
	*firstLine = 1;
	/*
	 * Cycle through the text in order to see how many linefeeds we pass.
	 */
	for (; i < left; i ++) {
		if (text [i] == '\n') {
			(*firstLine) ++;
		}
	}
	if (left == right) return false;
	*lastLine = *firstLine;
	for (; i < right; i ++) {
		if (text [i] == '\n') {
			(*lastLine) ++;
		}
	}
	Melder_free (text);
	return true;
}

static char32 *theFindString = nullptr, *theReplaceString = nullptr;
static void do_find (TextEditor me) {
	if (! theFindString) return;   // e.g. when the user does "Find again" before having done any "Find"
	long left, right;
	autostring32 text = GuiText_getStringAndSelectionPosition (my textWidget, & left, & right);
	char32 *location = str32str (& text [right], theFindString);
	if (location) {
		long index = location - text.peek();
		GuiText_setSelection (my textWidget, index, index + str32len (theFindString));
		GuiText_scrollToSelection (my textWidget);
		#ifdef _WIN32
			GuiThing_show (my d_windowForm);
		#endif
	} else {
		/* Try from the start of the document. */
		location = str32str (text.peek(), theFindString);
		if (location) {
			long index = location - text.peek();
			GuiText_setSelection (my textWidget, index, index + str32len (theFindString));
			GuiText_scrollToSelection (my textWidget);
			#ifdef _WIN32
				GuiThing_show (my d_windowForm);
			#endif
		} else {
			Melder_beep ();
		}
	}
}

static void do_replace (TextEditor me) {
	if (! theReplaceString) return;   // e.g. when the user does "Replace again" before having done any "Replace"
	autostring32 selection = GuiText_getSelection (my textWidget);
	if (! Melder_equ (selection.peek(), theFindString)) {
		do_find (me);
		return;
	}
	long left, right;
	autostring32 text = GuiText_getStringAndSelectionPosition (my textWidget, & left, & right);
	GuiText_replace (my textWidget, left, right, theReplaceString);
	GuiText_setSelection (my textWidget, left, left + str32len (theReplaceString));
	GuiText_scrollToSelection (my textWidget);
	#ifdef _WIN32
		GuiThing_show (my d_windowForm);
	#endif
}

static void menu_cb_find (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	EDITOR_FORM (U"Find", 0)
		LABEL (U"", U"Find:")
		TEXTFIELD (U"findString", U"")
	EDITOR_OK
		if (theFindString) SET_STRING (U"findString", theFindString);
	EDITOR_DO
		Melder_free (theFindString);
		theFindString = Melder_dup_f (GET_STRING (U"findString"));
		do_find (me);
	EDITOR_END
}

static void menu_cb_findAgain (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	do_find (me);
}

static void menu_cb_replace (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	EDITOR_FORM (U"Find", 0)
		LABEL (U"", U"This is a \"slow\" find-and-replace method;")
		LABEL (U"", U"if the selected text is identical to the Find string,")
		LABEL (U"", U"the selected text will be replaced by the Replace string;")
		LABEL (U"", U"otherwise, the next occurrence of the Find string will be selected.")
		LABEL (U"", U"So you typically need two clicks on Apply to get a text replaced.")
		LABEL (U"", U"Find:")
		TEXTFIELD (U"findString", U"")
		LABEL (U"", U"Replace with:")
		TEXTFIELD (U"replaceString", U"")
	EDITOR_OK
		if (theFindString != NULL) SET_STRING (U"findString", theFindString);
		if (theReplaceString != NULL) SET_STRING (U"replaceString", theReplaceString);
	EDITOR_DO
		Melder_free (theFindString);
		theFindString = Melder_dup (GET_STRING (U"findString"));
		Melder_free (theReplaceString);
		theReplaceString = Melder_dup (GET_STRING (U"replaceString"));
		do_replace (me);
	EDITOR_END
}

static void menu_cb_replaceAgain (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	do_replace (me);
}

static void menu_cb_whereAmI (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	long numberOfLinesLeft, numberOfLinesRight;
	if (! getSelectedLines (me, & numberOfLinesLeft, & numberOfLinesRight)) {
		Melder_information (U"The cursor is on line ", numberOfLinesLeft, U".");
	} else if (numberOfLinesLeft == numberOfLinesRight) {
		Melder_information (U"The selection is on line ", numberOfLinesLeft, U".");
	} else {
		Melder_information (U"The selection runs from line ", numberOfLinesLeft, U" to line ", numberOfLinesRight, U".");
	}
}

static void menu_cb_goToLine (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	EDITOR_FORM (U"Go to line", 0)
		NATURAL (U"Line", U"1")
	EDITOR_OK
		long firstLine, lastLine;
		getSelectedLines (me, & firstLine, & lastLine);
		SET_INTEGER (U"Line", firstLine);
	EDITOR_DO
		autostring32 text = GuiText_getString (my textWidget);
		long lineToGo = GET_INTEGER (U"Line"), currentLine = 1;
		int64 left = 0, right = 0;
		if (lineToGo == 1) {
			for (; text [right] != U'\n' && text [right] != U'\0'; right ++) { }
		} else {
			for (; text [left] != U'\0'; left ++) {
				if (text [left] == U'\n') {
					currentLine ++;
					if (currentLine == lineToGo) {
						left ++;
						for (right = left; text [right] != U'\n' && text [right] != U'\0'; right ++) { }
						break;
					}
				}
			}
		}
		if (left == str32len (text.peek())) {
			right = left;
		} else if (text [right] == U'\n') {
			right ++;
		}
		GuiText_setSelection (my textWidget, left, right);
		GuiText_scrollToSelection (my textWidget);
	EDITOR_END
}

static void menu_cb_convertToCString (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	autostring32 text = GuiText_getString (my textWidget);
	char32 buffer [2] = U" ";
	const char32 *hex [16] = { U"0", U"1", U"2", U"3", U"4", U"5", U"6", U"7", U"8", U"9", U"A", U"B", U"C", U"D", U"E", U"F" };
	MelderInfo_open ();
	MelderInfo_write (U"\"");
	for (char32 *p = & text [0]; *p != U'\0'; p ++) {
		char32 kar = *p;
		if (kar == U'\n') {
			MelderInfo_write (U"\\n\"\n\"");
		} else if (kar == U'\t') {
			MelderInfo_write (U"   ");
		} else if (kar == U'\"') {
			MelderInfo_write (U"\\\"");
		} else if (kar == U'\\') {
			MelderInfo_write (U"\\\\");
		} else if (kar > 127) {
			if (kar <= 0x00FFFF) {
				MelderInfo_write (U"\\u", hex [kar >> 12], hex [(kar >> 8) & 0x00000F], hex [(kar >> 4) & 0x00000F], hex [kar & 0x00000F]);
			} else {
				MelderInfo_write (U"\\U", hex [kar >> 28], hex [(kar >> 24) & 0x00000F], hex [(kar >> 20) & 0x00000F], hex [(kar >> 16) & 0x00000F],
					hex [(kar >> 12) & 0x00000F], hex [(kar >> 8) & 0x00000F], hex [(kar >> 4) & 0x00000F], hex [kar & 0x00000F]);
			}
		} else {
			buffer [0] = *p;
			MelderInfo_write (& buffer [0]);
		}
	}
	MelderInfo_write (U"\"");
	MelderInfo_close ();
}

/***** 'Font' menu *****/

static void updateSizeMenu (TextEditor me) {
	if (my fontSizeButton_10) GuiMenuItem_check (my fontSizeButton_10, my p_fontSize == 10);
	if (my fontSizeButton_12) GuiMenuItem_check (my fontSizeButton_12, my p_fontSize == 12);
	if (my fontSizeButton_14) GuiMenuItem_check (my fontSizeButton_14, my p_fontSize == 14);
	if (my fontSizeButton_18) GuiMenuItem_check (my fontSizeButton_18, my p_fontSize == 18);
	if (my fontSizeButton_24) GuiMenuItem_check (my fontSizeButton_24, my p_fontSize == 24);
}
static void setFontSize (TextEditor me, int fontSize) {
	GuiText_setFontSize (my textWidget, fontSize);
	my pref_fontSize () = my p_fontSize = fontSize;
	updateSizeMenu (me);
}

static void menu_cb_10 (EDITOR_ARGS) { EDITOR_IAM (TextEditor); setFontSize (me, 10); }
static void menu_cb_12 (EDITOR_ARGS) { EDITOR_IAM (TextEditor); setFontSize (me, 12); }
static void menu_cb_14 (EDITOR_ARGS) { EDITOR_IAM (TextEditor); setFontSize (me, 14); }
static void menu_cb_18 (EDITOR_ARGS) { EDITOR_IAM (TextEditor); setFontSize (me, 18); }
static void menu_cb_24 (EDITOR_ARGS) { EDITOR_IAM (TextEditor); setFontSize (me, 24); }
static void menu_cb_fontSize (EDITOR_ARGS) {
	EDITOR_IAM (TextEditor);
	EDITOR_FORM (U"Text window: Font size", 0)
		NATURAL (U"Font size (points)", U"12")
	EDITOR_OK
		SET_INTEGER (U"Font size", (long) my p_fontSize);
	EDITOR_DO
		setFontSize (me, GET_INTEGER (U"Font size"));
	EDITOR_END
}

static void gui_text_cb_change (I, GuiTextEvent /* event */) {
	iam (TextEditor);
	if (! my dirty) {
		my dirty = true;
		my v_nameChanged ();
	}
}

void structTextEditor :: v_createChildren () {
	textWidget = GuiText_createShown (d_windowForm, 0, 0, Machine_getMenuBarHeight (), 0, GuiText_SCROLLED);
	GuiText_setChangeCallback (textWidget, gui_text_cb_change, this);
}

void structTextEditor :: v_createMenus () {
	TextEditor_Parent :: v_createMenus ();

	if (v_fileBased ()) {
		Editor_addCommand (this, U"File", U"New", 'N', menu_cb_new);
		Editor_addCommand (this, U"File", U"Open...", 'O', menu_cb_open);
		Editor_addCommand (this, U"File", U"Reopen from disk", 0, menu_cb_reopen);
	} else {
		Editor_addCommand (this, U"File", U"Clear", 'N', menu_cb_clear);
	}
	Editor_addCommand (this, U"File", U"-- save --", 0, NULL);
	if (v_fileBased ()) {
		Editor_addCommand (this, U"File", U"Save", 'S', menu_cb_save);
		Editor_addCommand (this, U"File", U"Save as...", 0, menu_cb_saveAs);
	} else {
		Editor_addCommand (this, U"File", U"Save as...", 'S', menu_cb_saveAs);
	}
	Editor_addCommand (this, U"File", U"-- close --", 0, NULL);
	GuiText_setUndoItem (textWidget, Editor_addCommand (this, U"Edit", U"Undo", 'Z', menu_cb_undo));
	GuiText_setRedoItem (textWidget, Editor_addCommand (this, U"Edit", U"Redo", 'Y', menu_cb_redo));
	Editor_addCommand (this, U"Edit", U"-- cut copy paste --", 0, NULL);
	Editor_addCommand (this, U"Edit", U"Cut", 'X', menu_cb_cut);
	Editor_addCommand (this, U"Edit", U"Copy", 'C', menu_cb_copy);
	Editor_addCommand (this, U"Edit", U"Paste", 'V', menu_cb_paste);
	Editor_addCommand (this, U"Edit", U"Erase", 0, menu_cb_erase);

	Editor_addMenu (this, U"Search", 0);
	Editor_addCommand (this, U"Search", U"Find...", 'F', menu_cb_find);
	Editor_addCommand (this, U"Search", U"Find again", 'G', menu_cb_findAgain);
	Editor_addCommand (this, U"Search", U"Replace...", GuiMenu_SHIFT + 'F', menu_cb_replace);
	Editor_addCommand (this, U"Search", U"Replace again", GuiMenu_SHIFT + 'G', menu_cb_replaceAgain);
	Editor_addCommand (this, U"Search", U"-- line --", 0, NULL);
	Editor_addCommand (this, U"Search", U"Where am I?", 0, menu_cb_whereAmI);
	Editor_addCommand (this, U"Search", U"Go to line...", 'L', menu_cb_goToLine);

	Editor_addMenu (this, U"Convert", 0);
	Editor_addCommand (this, U"Convert", U"Convert to C string", 0, menu_cb_convertToCString);

	Editor_addMenu (this, U"Font", 0);
	Editor_addCommand (this, U"Font", U"Font size...", 0, menu_cb_fontSize);
	fontSizeButton_10 = Editor_addCommand (this, U"Font", U"10", GuiMenu_CHECKBUTTON, menu_cb_10);
	fontSizeButton_12 = Editor_addCommand (this, U"Font", U"12", GuiMenu_CHECKBUTTON, menu_cb_12);
	fontSizeButton_14 = Editor_addCommand (this, U"Font", U"14", GuiMenu_CHECKBUTTON, menu_cb_14);
	fontSizeButton_18 = Editor_addCommand (this, U"Font", U"18", GuiMenu_CHECKBUTTON, menu_cb_18);
	fontSizeButton_24 = Editor_addCommand (this, U"Font", U"24", GuiMenu_CHECKBUTTON, menu_cb_24);
}

void TextEditor_init (TextEditor me, const char32 *initialText) {
	Editor_init (me, 0, 0, 600, 400, U"", nullptr);
	setFontSize (me, my p_fontSize);
	if (initialText) {
		GuiText_setString (my textWidget, initialText);
		my dirty = false;   // was set to true in valueChanged callback
		Thing_setName (me, U"");
	}
	if (! theOpenTextEditors) {
		theOpenTextEditors = Collection_create (classTextEditor, 100);
		Collection_dontOwnItems (theOpenTextEditors);
	}
	if (theOpenTextEditors) {
		Collection_addItem (theOpenTextEditors, me);
	}
}

autoTextEditor TextEditor_create (const char32 *initialText) {
	try {
		autoTextEditor me = Thing_new (TextEditor);
		TextEditor_init (me.peek(), initialText);
		return me;
	} catch (MelderError) {
		Melder_throw (U"Text window not created.");
	}
}

void TextEditor_showOpen (TextEditor me) {
	cb_showOpen (Editor_getMenuCommand (me, U"File", U"Open..."), nullptr, nullptr, nullptr);
}

/* End of file TextEditor.cpp */
