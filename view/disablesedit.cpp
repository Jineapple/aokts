/**
	AOK Trigger Studio (See aokts.cpp for legal conditions.)
	WINDOWS VERSION
	disablesedit.cpp -- Defines functions for Disables editor.

	VIEW/CONTROLLER
**/

#include "editors.h"
#include "../resource.h"
#include "LinkListBox.h"
#include "utilunit.h"
#include "utilui.h"

/* Disabled */

#define MAX_DISABLED 30
#define NUM_TYPES 3

enum DTypes
{ DIS_bldg, DIS_unit, DIS_tech };

const char *dtypes[NUM_TYPES] = { "Buildings", "Units", "Techs" };

const char d_title[] = "Disables Editor";

/**
 * Directly moves an entry for the "all" listbox to the "disabled" listbox by
 * listbox index.
 */
static void MoveToDisabled(HWND dialog, int index)
{
	HWND const list_all = GetDlgItem(dialog, IDC_D_ALL);
	HWND const list_dis = GetDlgItem(dialog, IDC_D_SEL);

	// Get Link pointer from "all" listbox entry.
	const Link * link = LinkListBox_Get(list_all, index);

	if (link == NULL)
	{
		MessageBox(
				dialog,
				"Error while retrieving entry for left list. Please report this.",
				"Disables editor",
				MB_ICONWARNING);
		return;
	}

	// Remove entry for "all" listbox
	SendMessage(list_all, LB_DELETESTRING, index, 0);

	// Add entry to "disabled" listbox with appropriate Link pointer.
	LinkListBox_Add(list_dis, link);
}

/**
 * Disables the unit or technology specified by the given id. Whether a unit or
 * technology is disabled depends on the current selection of the list in the
 * UI.
 */
static void DisableItem(HWND dialog, int id)
{
	// Get the index of the id in the "all" list.
	int index;
	HWND list_all = GetDlgItem(dialog, IDC_D_ALL);

	// Get the Link * from the appropriate list: techs or units.
	// Downcast with static_cast to make the condition operator work.
	Link const * link = (propdata.sel0 == DIS_tech) ?
		static_cast<Link const *>(esdata.techs.getById(id)) :
		esdata.units.getById(id);

	// And use the Link * to lookup the index. This is inefficient, but it's UI
	// speed anyway.
	index = LinkListBox_GetIndex(list_all, link);

	// Call MoveToDisabled() with the index.
	MoveToDisabled(dialog, index);
}

/**
 * Disables the item currently selected in "all" list.
 */
void DisableSelectedItem(HWND dialog)
{
	// Get current selection index
	LRESULT index = SendDlgItemMessage(dialog, IDC_D_ALL, LB_GETCURSEL, 0, 0);

	// Ensure there was a current selection before proceeding.
	if (index != LB_ERR)
	{
		// Call MoveToDisabled with the index.
		MoveToDisabled(dialog, index);
	}
	else
	{
		// No selection! There should have been, so alert the user.
		MessageBox(
				dialog,
				"No selection! Please report this error.",
				"Disables Editor",
				MB_ICONWARNING);
	}
}

void Disables_HandleDel(HWND dialog)
{
	WPARAM index;
	const Link * data;
	HWND list_sel;

	list_sel = GetDlgItem(dialog, IDC_D_SEL);

	index = SendMessage(list_sel, LB_GETCURSEL, 0, 0);

	if (index == LB_ERR)
		return; // TODO: error recovery?

	// Retrieve and remove from "Selected" list
	data = LinkListBox_Get(list_sel, index);
	SendMessage(list_sel, LB_DELETESTRING, index, 0);

	// Add link back to "All" list
	LinkListBox_Add(GetDlgItem(dialog, IDC_D_ALL), data);
}

void Disables_HandleClear(HWND dialog)
{
	HWND list_sel = GetDlgItem(dialog, IDC_D_SEL);
	HWND list_all = GetDlgItem(dialog, IDC_D_ALL);

	for (LRESULT i = SendMessage(list_sel, LB_GETCOUNT, 0, 0); i != 0; i--)
	{
		// Retrieve and remove from "Selected" list
		const Link * data = LinkListBox_Get(list_sel, 0);
		SendMessage(list_sel, LB_DELETESTRING, 0, 0);

		// Add link back to "All" list
		LinkListBox_Add(list_all, data);
	}
}

void LoadDisables(HWND dialog)
{
	HWND list_sel, list_all;
	int i;
	long *d_parse;

	list_sel = GetDlgItem(dialog, IDC_D_SEL);
	list_all = GetDlgItem(dialog, IDC_D_ALL);

	SendMessage(list_sel, LB_RESETCONTENT, 0, 0);
	SendMessage(list_all, LB_RESETCONTENT, 0, 0);

	switch (propdata.sel0)
	{
	case DIS_bldg:

		if (esdata.ug_buildings)
			UnitList_FillGroup(list_all, esdata.ug_buildings);
		else
			MessageBox(dialog, "Could not load building list.", d_title, MB_ICONWARNING);

		d_parse = propdata.p->dis_bldg;
		for (i = 0; i < propdata.p->ndis_b; i++)
			DisableItem(dialog, *d_parse++);

		break;

	case DIS_tech:

		LinkListBox_Fill(list_all, esdata.techs.head());

		d_parse = propdata.p->dis_tech;
		for (i = 0; i < propdata.p->ndis_t; i++)
		{
			if (*d_parse > 0)
				DisableItem(dialog, *d_parse++);
		}

		break;

	case DIS_unit:

		if (esdata.ug_units)
			UnitList_FillGroup(list_all, esdata.ug_units);
		else
			MessageBox(dialog, "Could not load unit list.", d_title, MB_ICONWARNING);

		d_parse = propdata.p->dis_unit;
		for (i = 0; i < propdata.p->ndis_u; i++)
			DisableItem(dialog, *d_parse++);
		break;
	}
}

void SaveDisables(HWND dialog)
{
	int count;
	HWND list_sel;
	long *array;

	list_sel = GetDlgItem(dialog, IDC_D_SEL);
	count = SendMessage(list_sel, LB_GETCOUNT, 0, 0);

	/* count must be limited to MAX_DISABLED */
	if (count > MAX_DISABLED)
		count = MAX_DISABLED;	//TODO: buildings?

	switch (propdata.sel0)
	{
	case DIS_bldg:
		propdata.p->ndis_b = count;
		array = propdata.p->dis_bldg;
		break;
		
	case DIS_tech:
		propdata.p->ndis_t = count;
		array = propdata.p->dis_tech;
		break;
		
	case DIS_unit:
		propdata.p->ndis_u = count;
		array = propdata.p->dis_unit;
		break;

	default:
		return; // at least don't crash below
	}

	for (int i = 0; i < count; i++)
	{
		const Link *t = LinkListBox_Get(list_sel, i);
		if (t) // TODO: error handling?
			*array++ = t->id();
	}
}

void Disables_HandleCommand(HWND dialog, WORD code, WORD id, HWND control)
{
	switch (code)
	{
	case BN_CLICKED:
	case CBN_SELCHANGE:
		switch (id)
		{
		case IDC_D_SPLY:
			SaveDisables(dialog);
			propdata.pindex = SendMessage(control, CB_GETCURSEL, 0, 0);
			propdata.p = &scen.players[propdata.pindex];
			LoadDisables(dialog);
			break;

		case IDC_D_STYPE:
			SaveDisables(dialog);
			propdata.sel0 = SendMessage(control, CB_GETCURSEL, 0, 0);
			LoadDisables(dialog);
			break;

		case IDC_D_ADD:
			DisableSelectedItem(dialog);
			break;

		case IDC_D_DEL:
			Disables_HandleDel(dialog);
			break;

		case IDC_D_CLR:
			Disables_HandleClear(dialog);
			break;

		case ID_EDIT_COPY:
			SendMessage(GetFocus(), WM_COPY, 0, 0);
			break;

		case ID_EDIT_CUT:
			SendMessage(GetFocus(), WM_CUT, 0, 0);
			break;

		case ID_EDIT_PASTE:
			SendMessage(GetFocus(), WM_PASTE, 0, 0);
			break;
		}
		break;

	case EN_SETFOCUS:
		EnableMenuItem(propdata.menu, ID_EDIT_COPY, MF_ENABLED);
		EnableMenuItem(propdata.menu, ID_EDIT_CUT, MF_ENABLED);
		if (IsClipboardFormatAvailable(CF_TEXT))
			EnableMenuItem(propdata.menu, ID_EDIT_PASTE, MF_ENABLED);
		break;

	case EN_KILLFOCUS:
		EnableMenuItem(propdata.menu, ID_EDIT_COPY, MF_GRAYED);
		EnableMenuItem(propdata.menu, ID_EDIT_CUT, MF_GRAYED);
		EnableMenuItem(propdata.menu, ID_EDIT_PASTE, MF_GRAYED);
		break;
	}
}

/**
 * Resets the dialog when new data is available, such as when activating the
 * dialog.
 */
static void reset(HWND dialog)
{
	propdata.sel0 = 0;
	LoadDisables(dialog);
	SendDlgItemMessage(dialog, IDC_D_SPLY, CB_SETCURSEL, propdata.pindex, 0);
	SendDlgItemMessage(dialog, IDC_D_STYPE, CB_SETCURSEL, 0, 0);
}

INT_PTR CALLBACK DisDlgProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam)
{
	try
	{
		switch (msg)
		{
		case WM_INITDIALOG:
			Combo_Fill(dialog, IDC_D_SPLY, Player::names, NUM_PLAYERS - 1);
			Combo_Fill(dialog, IDC_D_STYPE, dtypes, NUM_TYPES);
			return TRUE;

		case WM_COMMAND:
			Disables_HandleCommand(
				dialog, HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
			break;

		case WM_NOTIFY:
			{
				NMHDR *header = (NMHDR*)lParam;
				switch (header->code)
				{
				case PSN_SETACTIVE:
					reset(dialog);
					break;
				case PSN_KILLACTIVE:
					SaveDisables(dialog);
					break;
				}
			}
			break;

		case AOKTS_Loading:
			reset(dialog);
			return 0;

		case AOKTS_Saving:
			SaveDisables(dialog);
		}
	}
	catch (std::exception& ex)
	{
		// Show a user-friendly message, bug still crash to allow getting all
		// the debugging info.
		unhandledExceptionAlert(dialog, msg, ex);
		throw;
	}

	return 0;
}