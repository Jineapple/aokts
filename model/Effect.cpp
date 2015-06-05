#include "Effect.h"
#include <sstream>
#include <algorithm>
#include "scen.h"
#include "TriggerVisitor.h"

#include "../view/utilunit.h"
#include "../util/utilio.h"
#include "../view/utilui.h"
#include "../util/Buffer.h"
#include "../util/helper.h"
#include "../util/settings.h"
#include "../util/cpp11compat.h"

using std::vector;

extern class Scenario scen;

#pragma pack(push, 4)
// An effect as stored in scenario, up to strings
struct Genie_Effect
{
	long type;
	long check;
	long ai_goal;
	long amount;
	long resource;
	long diplomacy;
	long num_selected;
	long uid_location;
	long unit_constant;
	long player_source;
	long player_target;
	long technology;
	long textid;
	long soundid;
	long display_time;
	long trigger_index;
	AOKPT location;
	AOKRECT area;
	long unit_group;
	long unit_type;
	long panel;
};
#pragma pack(pop)

Effect::Effect()
:	ECBase(EFFECT),
	ai_goal(-1),
	amount(-1),
	res_type(-1),
	diplomacy(-1),
	num_sel(-1),
	uid_loc(-1),
	pUnit(NULL),
	s_player(-1),
	t_player(-1),
	pTech(NULL),
	textid(-1),
	soundid(-1),
	disp_time(-1),
	trig_index(-1),
	// location default ctor fine
	// area default ctor fine
	group(-1),
	utype(-1),
	panel(-1),
	valid_since_last_check(true)
{
	memset(uids, -1, sizeof(uids));
}

//Effect::Effect( const Effect& other ) {
//}
//
//Effect::~Effect()
//{
//}

Effect::Effect(Buffer &b)
:	ECBase(EFFECT)
{
	// read flat data
	Genie_Effect genie;
	b.read(&genie, sizeof(genie));
	std::swap(genie.type, genie.check); // HACK: un-swap type, check
	fromGenie(genie);

	// move on to non-flat data
	text.read(b, sizeof(long));
	sound.read(b, sizeof(long));
	if (num_sel > 0)
		b.read(uids, sizeof(uids));

	check_and_save();
}

void Effect::tobuffer(Buffer &b) const
{
	/* Even though the Genie format sucks, we use it for Buffer operations
	 * (i.e., copy & paste) since it's easier to maintain one sucky format than
	 * one sucky and one slightly-less-sucky format.
	 */

	// write flat data
	Genie_Effect genie = toGenie();
	std::swap(genie.type, genie.check); // HACK: swap type, check
	b.write(&genie, sizeof(genie));

	text.write(b, sizeof(long));
	sound.write(b, sizeof(long));
	if (num_sel > 0)
		b.write(uids, sizeof(uids));
}

void Effect::read(FILE *in)
{
	Genie_Effect genie;

	readbin(in, &genie);
	fromGenie(genie);

	text.read(in, sizeof(long));
	sound.read(in, sizeof(long));

    // this helped me find why there was heap corruption
    // && num_sel < 22
    // Need a bigger number for max selected units
	if (num_sel > 0)
		readbin(in, uids, num_sel);

    // DODGY! should be removed as soon as bug is fixed
	if ((long)trig_index < 0) {
	    trig_index = -1;
	}

	check_and_save();
}

void Effect::write(FILE *out)
{
	Genie_Effect genie = toGenie();
	writebin(out, &genie);

	text.write(out, sizeof(long));
	sound.write(out, sizeof(long));
	if (num_sel > 0)
		fwrite(uids, sizeof(long), num_sel, out);
}

std::string pluralizeUnitType(std::string unit_type_name) { 
    std::ostringstream convert;
    replaced(unit_type_name, ", Unpacked", "");
    if (!unit_type_name.empty() && *unit_type_name.rbegin() != 's' && !replaced(unit_type_name, "man", "men")) {
        convert << unit_type_name << "s";
    } else {
        convert << unit_type_name;
    }
    return convert.str();
}

inline std::string unitTypeName(const UnitLink *pUnit) {
    std::ostringstream convert;
    if (pUnit && pUnit->id()) {
        std::wstring unitname(pUnit->name());
        std::string un(unitname.begin(), unitname.end());
        convert << un;
    } else {
        convert << "unit";
    }
    return convert.str();
}

std::string Effect::selectedUnits() const {
    std::ostringstream convert;
    bool class_selected = group >= 0 && group < NUM_GROUPS - 1;
    bool unit_type_selected = utype >= 0 && utype < NUM_UTYPES - 1;
    if (s_player >= 0)
        convert << playerPronoun(s_player) << " ";
    if (pUnit && pUnit->id()) {
        if (num_sel > 1) {
            convert << pluralizeUnitType(unitTypeName(pUnit));
        } else {
            convert << unitTypeName(pUnit);
        }
    } else if (class_selected || unit_type_selected) { // account for groups[0]={-1,None}
        if (class_selected && unit_type_selected) {
            convert << "units of class " << groups[group + 1].name;
            convert << "and type " << groups[group + 1].name;
        } else if (class_selected) {
            convert << "units of class " << groups[group + 1].name;
        } else {
            convert << "units of type " << groups[group + 1].name;
        }
    } else {
        convert << "units";
    }
	for (int i = 0; i < num_sel; i++) {
        convert << " " << uids[i] << " (" << get_unit_full_name(uids[i]) << ")";
	}
    return convert.str();
}

// functor used for getting a subset of an array of PlayerUnits
class playersunit_ucnst_equals
{
public:
	playersunit_ucnst_equals(UCNST cnst)
		: _cnst(cnst)
	{
	}

	bool operator()(const PlayersUnit& pu)
	{
	    bool sametype = false;
        if (pu.u) {
            const UnitLink * ul = pu.u->getType();
	        if (ul->id() == _cnst) {
	            sametype = true;
	        }
        }
		return (sametype);
	}

private:
	UCNST _cnst;
};

// functor used for getting a subset of an array of PlayerUnits
class playersunit_ucnst_notequals
{
public:
	playersunit_ucnst_notequals(UCNST cnst)
		: _cnst(cnst)
	{
	}

	bool operator()(const PlayersUnit& pu)
	{
	    bool difftype = false;
        if (pu.u) {
            const UnitLink * ul = pu.u->getType();
	        if (ul->id() != _cnst) {
	            difftype = true;
	        }
        }
		return (difftype);
	}

private:
	UCNST _cnst;
};

std::string Effect::getAffectedUnits() const {
    std::ostringstream convert;
    bool unit_set_selected = valid_unit_spec(); // also use unit class and type
    convert << playerPronoun(s_player) << "'s ";
	if (num_sel > 0) {
	    if (num_sel == 1) {
            convert << "unit ";
        } else {
            convert << "units ";
        }
	    for (int i = 0; i < num_sel; i++) {
            convert << uids[i] << " (" << get_unit_full_name(uids[i]) << ") ";
	    }
	    if (unit_set_selected) {
	        convert << " and ";
	    }
    }
    if (unit_set_selected) {
        std::wstring unitname(pUnit->name());
        std::string un(unitname.begin(), unitname.end());
        if (!un.empty() && *un.rbegin() != 's' && !replaced(un, "man", "men")) {
            convert << un << "s ";
        } else {
            convert << un << " ";
        }
    } else {
	    if (num_sel <= 0) {
            convert << "units ";
	    }
    }
    if (unit_set_selected) {
        if (valid_partial_map()) {
            if (valid_area_location()) {
                convert << "at (" << area.left << "," << area.top << ") ";
            } else {
                convert << "in area (" << area.left << "," << area.bottom << ") - (" << area.right << ", " << area.top << ") ";
            }
        }
    }
	return convert.str();
}

std::string Effect::getName(bool tip, NameFlags::Value flags) const
{
    if (!tip) {
	    return (type < scen.pergame->max_effect_types) ? getTypeName(type, false) : "Unknown!";
	} else {
        std::string stype = std::string("");
        std::ostringstream convert;
        switch (type) {
            case EffectType::None:
                // Let this act like a separator
                convert << "                                                                                    ";
                stype.append(convert.str());
                break;
            case EffectType::ChangeDiplomacy:
                convert << playerPronoun(s_player);
                switch (diplomacy) {
                case 0:
                    convert << " allies with ";
                    break;
                case 1:
                    convert << " becomes neutral to ";
                    break;
                case 2:
                    convert << " <INVALID DIPLOMACY> to ";
                    break;
                case 3:
                    convert << " declares war on ";
                    break;
                }
                convert << playerPronoun(t_player);
                stype.append(convert.str());
                break;
            case EffectType::ResearchTechnology:
                {
                    bool hastech = pTech && pTech->id();
                    std::wstring wtechname;
                    std::string techname;
                    if (hastech) {
                        wtechname = std::wstring(pTech->name());
                        techname = std::string(wtechname.begin(), wtechname.end());
                        if (panel >= 1 && panel <= 3) {
                        switch (panel) {
                        case 1:
                            convert << "enable " << playerPronoun(s_player) << " to research " << techname;
                            break;
                        case 2:
                            convert << "disable " << playerPronoun(s_player) << " to research " << techname;
                            break;
                        case 3:
                            convert << "enable " << playerPronoun(s_player) << " to research " << techname << " regardless of civ";
                            break;
                        }
                        } else {
                            convert << playerPronoun(s_player);
                            convert << " researches " << techname;
                        }
                    } else {
                        convert << "INVALID";
                    }
                }
                stype.append(convert.str());
                break;
            case EffectType::SendChat:
                switch (s_player) {
                    case -1:
                    case 0:
                        convert << "tell everyone";
                        break;
                    default:
                        convert << "tell p" << s_player;
                }
                convert << " \"" << text.c_str() << "\"";
                stype.append(convert.str());
                break;
            case EffectType::Sound:
                convert << "play sound " << sound.c_str();
                stype.append(convert.str());
                break;
            case EffectType::SendTribute:
                // 9999999999 result in 1410065407 because 9999999999 =
                // 2*2^32 + 1410065407 and a long has 32 bits only.
                if (amount == 1410065407) {
                    // reset to 0
                    convert << "reset ";
                    convert << playerPronoun(s_player) << "'s";
                } else if (amount >= 0) {
                    convert << playerPronoun(s_player);
                    if (t_player == 0) {
                        convert << " loses";
                    } else {
                        convert << " gives ";
                        convert << "p" << t_player;
                    }
                    convert << " " << amount << " ";
                } else {
                    if (t_player == 0) {
                        if (res_type < 4) {
                            convert << "p" << s_player << " silently gets " << -amount << " ";
                        } else {
                            convert << "p" << s_player << " gets " << -amount << " ";
                        }
                    } else {
                        convert << "p" << t_player << " silently gives ";
                        convert << playerPronoun(s_player);
                        convert << " " << -amount << " ";
                    }
                }
                switch (res_type) {
                    case 0: // Food
                        convert << "food";
                        break;
                    case 1: // Wood
                        convert << "wood";
                        break;
                    case 2: // Stone
                        convert << "stone";
                        break;
                    case 3: // Gold
                        convert << "gold";
                        break;
                    case 20: // Units killed
                        if (amount == 1) {
                            convert << "kill";
                        } else {
                            convert << "kills";
                        }
                        break;
                    default:
                        //convert << types_short[type];
                        if (res_type >= 0) {
                            const Link * list = esdata.resources.head();
	                        for (int i=0; list; list = list->next(), i++)
	                        {
		                        if (i == res_type) {
                                    std::wstring resname(list->name());
                                    convert << std::string( resname.begin(), resname.end());
		                            break;
		                        }
	                        }
	                    }
                        break;
                }
                if (amount == 1410065407) {
                    convert << " to 0";
                }
                if (amount >= 0 && t_player > 0) {
                    convert << " (displays tribute alert)";
                }
                stype.append(convert.str());
                break;
            case EffectType::ActivateTrigger:
            case EffectType::DeactivateTrigger:
                //stype.append(types_short[type]);
                switch (type) {
                    case EffectType::ActivateTrigger:
                        stype.append("activate ");
                        break;
                    case EffectType::DeactivateTrigger:
                        stype.append("deactivate ");
                        break;
                }
                if (trig_index != (unsigned)-1 && trig_index != (unsigned)-2) {
                    if (trig_index < scen.triggers.size() && trig_index >= 0) {
                        if (setts.showdisplayorder) {
                            stype.append("<").append(toString(scen.triggers.at(trig_index).display_order)).append("> ");
                        }
                        // can't make recursion > 0 until I find a way
                        // to limit it
                        stype.append(scen.triggers.at(trig_index).getName(setts.pseudonyms,true,0));
                    } else {
                        stype.append("<?>");
                    }
                    //convert << trig_index;
                    //stype.append(convert.str());
                }
                break;
            case EffectType::AIScriptGoal:
                switch (ai_goal) {
                default:
                    if (ai_goal >= -258 && ai_goal <= -3) {
                        // AI Shared Goal
                        convert << "complete AI shared goal " << ai_goal + 258;
                    } else if (ai_goal >= 774 && ai_goal <= 1029) {
	                    // Set AI Signal
                        convert << "signal AI " << ai_goal - 774;
                    } else {
                        convert << "AI signalled " << ai_goal;
                    }
                }
                stype.append(convert.str());
                break;;
            case EffectType::ChangeView:
                if (location.x >= 0 && location.y >= 0 && s_player >= 1) {
                    convert << "change view for p" << s_player << " to (" << location.x << ", " << location.y << ")";
                } else {
                    convert << "INVALID";
                }
                stype.append(convert.str());
                break;
            case EffectType::UnlockGate:
            case EffectType::LockGate:
            case EffectType::KillObject:
            case EffectType::RemoveObject:
            case EffectType::FreezeUnit:
                switch (type) {
                case EffectType::UnlockGate:
                    convert << "unlock";
                    break;
                case EffectType::LockGate:
                    convert << "lock";
                    break;
                case EffectType::KillObject:
                    convert << "kill";
                    break;
                case EffectType::RemoveObject:
                    convert << "remove";
                    break;
                case EffectType::Unload:
                    convert << "unload";
                    break;
                case EffectType::FreezeUnit:
                    convert << "no attack stance";
                    break;
                }
                convert << " " << getAffectedUnits();
                stype.append(convert.str());
                break;
            case EffectType::Unload:
                convert << "unload " << selectedUnits() << " to (" << location.x << ", " << location.y << ")";
                stype.append(convert.str());
                break;

            case EffectType::StopUnit:
                if (panel >= 1 && panel <= 9) {
                    convert << "place" << " " << selectedUnits() << " into control group " << panel;
                } else {
                    convert << "Stop " << selectedUnits();
                    if (valid_partial_map()) {
                        if (valid_area_location()) {
                            convert << " at (" << area.left << "," << area.top << ")";
                        } else {
                            convert << " in area (" << area.left << ", " << area.bottom << ") - (" << area.right << ", " << area.top << ")";
                        }
                    }
                    if (valid_location()) {
                        convert << " at (" << location.x << ", " << location.y << ")";
                    }
                    stype.append(convert.str());
                    break;
                }
                stype.append(convert.str());
                break;
            case EffectType::CreateObject:
                convert << "create";
                convert << " " << playerPronoun(s_player);
                if (pUnit && pUnit->id()) {
                    std::wstring unitname(pUnit->name());
                    std::string un(unitname.begin(), unitname.end());
                    convert << " " << un;
                } else {
                    convert << " INVALID EFFECT";
                }
                convert << " at (" << location.x << ", " << location.y << ")";
                stype.append(convert.str());
                break;
            case EffectType::Patrol:
                try
                {
                    // keep in mind multiple units can possess the same id, but
                    // it only operates on the last farm with that id.
                    //
                    // s_player may not be selected. Therefore, go through all
                    // the units in scenario.
                    //
                    // A gaia farm could be made infinite.
                    //Player * p = scen.players + s_player;
                    //for (vector<Unit>::const_iterator iter = p->units.begin(); iter != sorted.end(); ++iter) {
                    //}

                    if (num_sel > 0) {
                        std::vector<PlayersUnit> all (num_sel);
	                    for (int i = 0; i < num_sel; i++) {
                            all[i] = find_map_unit(uids[i]);
	                    }
                        std::vector<PlayersUnit> farms (num_sel);
                        std::vector<PlayersUnit> other (num_sel);

                        std::vector<PlayersUnit>::iterator it;
                        it = copy_if (all.begin(), all.end(), farms.begin(), playersunit_ucnst_equals(50) );
                        farms.resize(std::distance(farms.begin(),it));  // shrink container to new size

                        it = copy_if (all.begin(), all.end(), other.begin(), playersunit_ucnst_notequals(50) );
                        other.resize(std::distance(other.begin(),it));  // shrink container to new size

                        if (farms.size() > 0) {
                            convert << "reseed ";
                            for(std::vector<PlayersUnit>::iterator it = farms.begin(); it != farms.end(); ++it) {
                                convert << it->u->ident << " (" <<
                                    get_unit_full_name(it->u->ident)
                                    << ")" << " ";
                            }
	                    }

                        if (other.size() > 0) {
                            convert << "patrol ";
                            for(std::vector<PlayersUnit>::iterator it = other.begin(); it != other.end(); ++it) {
                                convert << it->u->ident << " (" <<
                                    get_unit_full_name(it->u->ident)
                                    << ")" << " ";
                            }
	                    }
	                } else {
                        convert << "patrol / reseed nothing";
	                }
                    stype.append(convert.str());
                }
	            catch (std::exception& ex)
	            {
                    stype.append(ex.what());
	            }
                break;
            case EffectType::ChangeOwnership:
            case EffectType::TaskObject:
                switch (type) {
                    case EffectType::ChangeOwnership:
                        convert << "convert";
                        break;
                    case EffectType::TaskObject:
                        convert << "task";
                }
                convert << " " << selectedUnits();
                if (valid_partial_map()) {
                    if (valid_area_location()) {
                        convert << " at (" << area.left << "," << area.top << ")";
                    } else {
                        convert << " in (" << area.left << ", " << area.bottom << ") - (" << area.right << ", " << area.top << ")";
                    }
                }
                switch (type) {
                case EffectType::ChangeOwnership:
                    convert << " to";
                    convert << " " << playerPronoun(t_player);
                    break;
                case EffectType::TaskObject:
                    if (valid_location()) {
                        convert << " to (" << location.x << ", " << location.y << ")";
                    } else {
                        convert << " to unit " << uid_loc << " (" << get_unit_full_name(uid_loc) << ")";
                    }
                }
                stype.append(convert.str());
                break;
            case 13: // Declare victory
                convert << "p" << s_player << " victory";
                stype.append(convert.str());
                break;
            case 20: // Instructions
                convert << "instruct players \"" << text.c_str() << "\"";
                stype.append(convert.str());
                break;
            case 26: // Rename
                //stype.append(types_short[type]);
                stype.append("rename '");
                stype.append(text.c_str());
                stype.append("'");
                convert << " " << selectedUnits();
                if (valid_partial_map()) {
                    if (valid_area_location()) {
                        convert << " at (" << area.left << "," << area.top << ")";
                    } else {
                        convert << " over area (" << area.left << "," << area.bottom << ") - (" << area.right << ", " << area.top << ")";
                    }
                }
                stype.append(convert.str());
                break;
            case 24: // Damage
                {
                    if (amount == -(maxs31bit + 1)) {
                        convert << "zero health for " << getAffectedUnits();
                    } else if (amount == -maxs32bit) {
                        convert << "make " << getAffectedUnits() << "invincible";
                    } else {
                        if (amount < 0) {
                            convert << "buff " << getAffectedUnits() << "with " << -amount << " HP";
                        } else {
                            convert << "damage " << getAffectedUnits() << "by " << amount << " HP";
                        }
                    }
                    stype.append(convert.str());
                }
                break;
            case 27: // HP
            case 28: // Attack
            case 30: // UP Speed
            case 31: // UP Range
            case 32: // UP Armor1
            case 33: // UP Armor2
                {
                    if (amount > 0) {
                        convert << "+";
                    }
                    convert << amount << " " << getTypeName(type, true) << " to "  << getAffectedUnits();
                    stype.append(convert.str());
                }
                break;
            default:
                stype.append((type < scen.pergame->max_effect_types) ? getTypeName(type, true) : "Unknown!");
        }

        return flags&NameFlags::LIMITLEN?stype.substr(0,100):stype;
    }
}

const char * Effect::getTypeName(size_t type, bool concise) const
{
	return concise?types_short[type]:types[type];
}

int Effect::getPlayer() const
{
	return s_player;
}

void Effect::setPlayer(int player)
{
	s_player = player;
}

inline bool Effect::valid_full_map() const {
    return (area.left == -1 && area.right == -1 && area.bottom == -1 && area.top == -1);
}

inline bool Effect::valid_partial_map() const {
    return (area.left >=  0 && area.right >= area.left && area.bottom >=  0 && area.top >= area.bottom);
}

inline bool Effect::valid_area_location() const {
    return area.left == area.right && area.top == area.bottom;
}

inline bool Effect::valid_area() const {
    return valid_full_map() || valid_partial_map();
}

inline bool Effect::valid_selected() const {
	for (int i = 0; i < num_sel; i++) {
	    if (!valid_unit_id(uids[i])) {
		    return false;
	    }
	}
	return true;
}

inline bool Effect::valid_unit_spec() const {
    // utype >= 0
    // pUnit && pUnit->id() ??
	return pUnit != NULL && pUnit->id() >= 0;
}

inline bool Effect::valid_technology_spec() const {
	return pTech != NULL && pTech->id() >= 0;;
}

inline bool Effect::valid_location() const {
    return location.x >= 0 && location.y >= 0;
}

inline bool Effect::valid_source_player() const {
    return s_player >= 0 && s_player <= 8;
}

inline bool Effect::valid_target_player() const {
    return t_player >= 0 && t_player <= 8;
}

inline bool Effect::valid_trigger() const {
    return trig_index >= 0 &&  trig_index < scen.triggers.size();
}

inline bool Effect::valid_panel() const {
	// panel == -1 is acceptable because Azzzru's scripts omit panel
	// to shorten SCX file and scenario still works fine.
    return panel >= -1;
}

inline bool Effect::valid_destination() const {
	return valid_location() || valid_unit_id(uid_loc);
}

/*
 * Amount can be negative, but can't be -1 (or can it? it appears red in
 * scenario editor. It CAN be -1 (in AOHD at least)
 */
inline bool Effect::valid_points() const {
	//return (amount != -1);
	return true;
}

bool Effect::get_valid_since_last_check() {
    return valid_since_last_check;
}

bool Effect::check_and_save()
{
    return valid_since_last_check = check();
}

/*
 * False positives are better than false negatives.
 */
bool Effect::check() const
{
    if (type < 1 || type >= scen.pergame->max_effect_types) {
        return false;
    }

    bool valid_selected = Effect::valid_selected(); // perform this check on all effects

    if (num_sel > 0 && !valid_selected) {
        return false;
    }

	switch (type)
	{
	case EffectType::ChangeDiplomacy:
		return (valid_source_player() && valid_source_player() && diplomacy >= 0);

	case EffectType::ResearchTechnology:
		return (valid_source_player() && valid_technology_spec());

	case EffectType::SendChat:
		return (valid_source_player() && *text.c_str());	//AOK missing text check

	case EffectType::Sound:
		return (valid_source_player() && sound.length());	//AOK missing sound check

	case EffectType::SendTribute:
		return (valid_source_player() && valid_target_player() && res_type >= 0);

	case EffectType::UnlockGate:
	case EffectType::LockGate:
		return valid_selected;

	case EffectType::ActivateTrigger:
	case EffectType::DeactivateTrigger:
		return valid_trigger();

	case EffectType::AIScriptGoal:
		return true;

	case EffectType::CreateObject:
		return valid_source_player() && valid_location() && valid_unit_spec();

	case EffectType::Unload:
        return valid_selected && valid_destination();

	case EffectType::TaskObject:
        return (valid_selected || valid_partial_map()) && valid_destination();

	case EffectType::KillObject:
	case EffectType::RemoveObject:
	case EffectType::FreezeUnit:
	case EffectType::StopUnit:
	case EffectType::FlashUnit_SWGB:
		return valid_selected || valid_partial_map();

	case EffectType::DeclareVictory:
		return valid_source_player();

	case EffectType::ChangeView:
		return valid_source_player() && valid_location();

	case EffectType::ChangeOwnership:
		return (valid_selected || valid_partial_map()) && valid_source_player() && valid_source_player();

	case EffectType::Patrol:
		return (valid_selected && valid_location());

	case EffectType::DisplayInstructions:
		return (valid_panel() && disp_time >= 0 && (*text.c_str() || textid));	//AOK missing text

	case EffectType::ClearInstructions:
		return valid_panel();

	case EffectType::UseAdvancedButtons:
		return true;

	case EffectType::DamageObject:
	case EffectType::ChangeObjectHP:
	case EffectType::ChangeObjectAttack:
		return (valid_selected || (valid_unit_spec() && valid_area())) && valid_points();

	case EffectType::ChangeSpeed_UP: // SnapView_SWGB // AttackMove_HD
	    switch (scen.game) {
	    case UP:
		    return (valid_selected || (valid_unit_spec() && valid_area())) && valid_points();
	    case AOHD:
	    case AOF:
		    return valid_selected && valid_location();
	    case SWGB:
	    case SWGBCC:
		    return valid_source_player() && valid_location();
	    }
		return valid_location();

	case EffectType::ChangeRange_UP: // DisableAdvancedButtons_SWGB // ChangeArmor_HD
	    switch (scen.game) {
	    case UP:
	    case AOHD:
	    case AOF:
		    return (valid_selected || (valid_unit_spec() && valid_area())) && valid_points();
	    case SWGB:
	    case SWGBCC:
		    return true;
	    }
		return valid_location();

	case EffectType::ChangeMeleArmor_UP: // ChangeRange_HD // EnableTech_SWGB
	case EffectType::ChangePiercingArmor_UP: // ChangeSpeed_HD // DisableTech_SWGB
	    switch (scen.game) {
	    case UP:
	    case AOHD:
	    case AOF:
		    return (valid_selected || (valid_unit_spec() && valid_area())) && valid_points();
	    case SWGB:
	    case SWGBCC:
		    return valid_technology_spec();
	    }
		return true;

	case EffectType::PlaceFoundation:
		return (valid_source_player() && valid_unit_spec() && valid_location());

	case EffectType::ChangeObjectName:
	    switch (scen.game) {
	    case AOK:
	    case AOC:
	        return (valid_selected || (valid_unit_spec() && valid_area()));
	    case AOHD:
	    case AOF:
	        return (valid_selected || (valid_unit_spec() && valid_area()));
	    }
		return true;

	case EffectType::EnableUnit_SWGB:
	case EffectType::DisableUnit_SWGB:
		return valid_unit_spec();

	default:
		return false;	//unknown effect type
	}
}

void Effect::accept(TriggerVisitor& tv)
{
	tv.visit(*this);
}

void Effect::fromGenie(const Genie_Effect& genie)
{
	if (genie.check != EFFECT)
		throw bad_data_error("Effect has incorrect check value.");

	type = genie.type;
	ttype = static_cast<TType>(genie.check);
	ai_goal = genie.ai_goal;
	amount = genie.amount;
	res_type = genie.resource;
	diplomacy = genie.diplomacy;
	num_sel = genie.num_selected;
	uid_loc = genie.uid_location;
	pUnit = esdata.units.getByIdSafe(genie.unit_constant);
	s_player = genie.player_source;
	t_player = genie.player_target;
	pTech = esdata.techs.getByIdSafe(genie.technology);
	textid = genie.textid;
	soundid = genie.soundid;
	disp_time = genie.display_time;
	trig_index = genie.trigger_index;
	location = genie.location;
	area = genie.area;
	group = genie.unit_group;
	utype = genie.unit_type;
	panel = genie.panel;
}

Genie_Effect Effect::toGenie() const
{
	Genie_Effect ret =
	{
		type,
		ttype,
		ai_goal,
		amount,
		res_type,
		diplomacy,
		num_sel,
		uid_loc,
		(pUnit) ? pUnit->id() : -1,
		s_player,
		t_player,
		(pTech) ? pTech->id() : -1,
		textid,
		soundid,
		disp_time,
		trig_index,
		location,
		area,
		group,
		utype,
		panel
	};

	return ret;
}

const char *Effect::types_aok[] = {
	"",
	"Change Diplomacy",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Send Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol Units / Reseed Farms",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Use Advanced Buttons"
};

const char *Effect::types_aoc[] = {
	"",
	"Change Diplomacy",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Send Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol Units / Reseed Farms",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Use Advanced Buttons",
	"Damage Object",
	"Place Foundation",
	"Change Object Name",
	"Change Object HP (Change Max)",
	"Change Object Attack",
	"Stop Unit"
};

const char *Effect::types_up[] = {
	"",
	"Change Diplomacy",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Send Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol Units / Reseed Farms",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Use Advanced Buttons",
	"Damage Object",
	"Place Foundation",
	"Change Object Name",
	"Change Object HP (Change Max)",
	"Change Object Attack",
	"Stop Unit",
	"Change Speed",
	"Change Range",
	"Change Mele Armor",
	"Change Piercing Armor"
};

const char *Effect::types_swgb[] = {
	"",
	"Change Alliance",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Scroll View",
	"Unload",
	"Change Ownership",
	"Patrol Units",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Enable Advanced Buttons",
	"Damage Object (Change Health)",
	"Place Foundation",
	"Change Object Name",
	"Change Object HP (Change Max)",
	"Change Object Attack",
	"Stop Unit",
	"Snap View",
	"Disable Advanced Buttons",
	"Enable Tech",
	"Disable Tech",
	"Enable Unit",
	"Disable Unit",
	"Flash Objects"
};

const char *Effect::types_cc[] = {
	"",
	"Change Alliance",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Scroll View",
	"Unload",
	"Change Ownership",
	"Patrol Units",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Enable Advanced Buttons",
	"Damage Object (Change Health)",
	"Place Foundation",
	"Change Object Name",
	"Change Object HP (Change Max)",
	"Change Object Attack",
	"Stop Unit",
	"Snap View",
	"Disable Advanced Buttons",
};

const char *Effect::types_aohd[] = {
	"",
	"Change Diplomacy",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Send Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol Units / Reseed Farms",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Use Advanced Buttons",
	"Damage Object (Change Health)",
	"Place Foundation",
	"Change Object Name",
	"Change Object HP (Change Max)",
	"Change Object Attack",
	"Stop Unit",
	"Attack-Move",
	"Change Armor",
	"Change Range",
	"Change Speed",
};

const char *Effect::types_aof[] = {
	"",
	"Change Diplomacy",
	"Research Technology",
	"Send Chat",
	"Play Sound",
	"Send Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate Trigger",
	"Deactivate Trigger",
	"AI Script Goal",
	"Create Object",
	"Task Object",
	"Declare Victory",
	"Kill Object (Health=0)",
	"Remove Object",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol Units / Reseed Farms",
	"Display Instructions",
	"Clear Instructions",
	"Freeze Unit (No Attack Stance)",
	"Use Advanced Buttons",
	"Damage Object (Change Health)",
	"Place Foundation",
	"Change Object Name",
	"Change Object HP (Change Max)",
	"Change Object Attack",
	"Stop Unit",
	"Attack-Move",
	"Change Armor",
	"Change Range",
	"Change Speed",
};

const char *Effect::types_short_aok[] = {
	"",
	"Change Diplomacy",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol / Reseed",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons"
};

const char *Effect::types_short_aoc[] = {
	"",
	"Change Diplomacy",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol / Reseed",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons",
	"Damage",
	"Place Foundation",
	"Rename",
	"HP",
	"Attack",
	"Stop Unit",
};

const char *Effect::types_short_up[] = {
	"",
	"Change Diplomacy",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol / Reseed",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons",
	"Damage",
	"Place Foundation",
	"Rename",
	"HP",
	"Attack",
	"Stop Unit",
	"Speed",
	"Range",
	"Mele Armor",
	"Piercing Armor"
};

const char *Effect::types_short_aohd[] = {
	"",
	"Change Diplomacy",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol / Reseed",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons",
	"Damage",
	"Place Foundation",
	"Rename",
	"HP",
	"Attack",
	"Stop Unit",
	"Attack-Move",
	"Armor",
	"Range",
	"Speed"
};

const char *Effect::types_short_aof[] = {
	"",
	"Change Diplomacy",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Change View",
	"Unload",
	"Change Ownership",
	"Patrol / Reseed",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons",
	"Damage",
	"Place Foundation",
	"Rename",
	"HP",
	"Attack",
	"Stop Unit",
	"Attack-Move",
	"Armor",
	"Range",
	"Speed"
};

const char *Effect::types_short_swgb[] = {
	"",
	"Change Alliance",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Scroll View",
	"Unload",
	"Change Ownership",
	"Patrol",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons",
	"Damage",
	"Place Foundation",
	"Rename",
	"HP",
	"Attack",
	"Stop Unit",
	"Snap View",
	"Disable Advanced",
	"Enable Tech",
	"Disable Tech",
	"Enable Unit",
	"Disable Unit",
	"Flash"
};

const char *Effect::types_short_cc[] = {
	"",
	"Change Alliance",
	"Research",
	"Chat",
	"Sound",
	"Tribute",
	"Unlock Gate",
	"Lock Gate",
	"Activate",
	"Deactivate",
	"AI Script Goal",
	"Create",
	"Task",
	"Declare Victory",
	"Kill",
	"Remove",
	"Scroll View",
	"Unload",
	"Change Ownership",
	"Patrol",
	"Instructions",
	"Clear Instructions",
	"Freeze (No Attack)",
	"Use Advanced Buttons",
	"Damage",
	"Place Foundation",
	"Rename",
	"HP",
	"Attack",
	"Stop Unit",
	"Snap View",
	"Disable Advanced",
	"Enable Tech",
	"Disable Tech",
	"Enable Unit",
	"Disable Unit",
	"Flash",
	"Input Off",
	"Input On"
};

const char *Effect::virtual_types_up[] = {
    "",
    "Enable Object",
    "Disable Object",
    "Enable Technology",
    "Disable Technology",
    "Enable Tech (Any Civ)",
    "Set HP",
    "Heal Object",
    "Aggressive Stance",
    "Defensive Stance",
    "Stand Ground",
    "No Attack Stance",
    "Resign",
    "Flash Objects",
    "Set AP",
    "Set Control Group 1",
    "Set Control Group 2",
    "Set Control Group 3",
    "Set Control Group 4",
    "Set Control Group 5",
    "Set Control Group 6",
    "Set Control Group 7",
    "Set Control Group 8",
    "Set Control Group 9",
    "Snap View",
    "Zero Health",
    "Invincible",
    "Set AI Signal",
    "Set AI Shared Goal",
    "Enable Cheats",
};

const char *Effect::virtual_types_aoc[] = {
    "",
    "Zero Health",
    "Invincible",
    "Set AI Signal",
    "Set AI Shared Goal",
    "Enable Cheats",
    "Freeze unit",
};

const char *Effect::virtual_types_aohd[] = {
    "",
    "Zero Health",
    "Invincible",
    "Freeze unit",
};

const char *Effect::virtual_types_aof[] = {
    "",
    "Zero Health",
    "Invincible",
    "Freeze unit",
};

const char *Effect::virtual_types_swgb[] = {
    "",
    "Zero Health",
    "Invincible",
    "Freeze unit"
};

const char *Effect::virtual_types_cc[] = {
    "",
    "Zero Health",
    "Invincible",
    "Freeze unit"
};

const char *Effect::virtual_types_aok[] = {
    ""
    "Zero Health",
    "Invincible",
};

const char** Effect::types;
const char** Effect::types_short;
const char** Effect::virtual_types;
