#include "bonuses.h"

#include <algorithm>
#include <string>
#include <utility>

#include "character.h"
#include "damage.h"
#include "json.h"
#include "make_static.h"
#include "string_formatter.h"
#include "translations.h"

static bool needs_damage_type( affected_stat as )
{
    return as == affected_stat::DAMAGE ||
           as == affected_stat::ARMOR ||
           as == affected_stat::ARMOR_PENETRATION;
}

static const std::map<std::string, affected_stat> affected_stat_map = {{
        std::make_pair( "hit", affected_stat::HIT ),
        std::make_pair( "crit_chance", affected_stat::CRITICAL_HIT_CHANCE ),
        std::make_pair( "dodge", affected_stat::DODGE ),
        std::make_pair( "block", affected_stat::BLOCK ),
        std::make_pair( "block_effectiveness", affected_stat::BLOCK_EFFECTIVENESS ),
        std::make_pair( "speed", affected_stat::SPEED ),
        std::make_pair( "movecost", affected_stat::MOVE_COST ),
        std::make_pair( "damage", affected_stat::DAMAGE ),
        std::make_pair( "armor", affected_stat::ARMOR ),
        std::make_pair( "arpen", affected_stat::ARMOR_PENETRATION ),
        std::make_pair( "target_armor_multiplier", affected_stat::TARGET_ARMOR_MULTIPLIER )
    }
};

static const std::map<std::string, scaling_stat> scaling_stat_map = {{
        std::make_pair( "str", STAT_STR ),
        std::make_pair( "dex", STAT_DEX ),
        std::make_pair( "int", STAT_INT ),
        std::make_pair( "per", STAT_PER ),
    }
};

static scaling_stat scaling_stat_from_string( const std::string &s )
{
    const auto &iter = scaling_stat_map.find( s );
    if( iter == scaling_stat_map.end() ) {
        return STAT_NULL;
    }

    return iter->second;
}

static affected_stat affected_stat_from_string( const std::string &s )
{
    const auto &iter = affected_stat_map.find( s );
    if( iter != affected_stat_map.end() ) {
        return iter->second;
    }

    return affected_stat::NONE;
}

static const std::map<affected_stat, std::string> affected_stat_map_translation = {{
        std::make_pair( affected_stat::HIT, translate_marker( "Accuracy" ) ),
        std::make_pair( affected_stat::CRITICAL_HIT_CHANCE, translate_marker( "Critical Hit Chance" ) ),
        std::make_pair( affected_stat::DODGE, translate_marker( "Dodge" ) ),
        std::make_pair( affected_stat::BLOCK, translate_marker( "Block" ) ),
        std::make_pair( affected_stat::BLOCK_EFFECTIVENESS, translate_marker( "Block effectiveness" ) ),
        std::make_pair( affected_stat::SPEED, translate_marker( "Speed" ) ),
        std::make_pair( affected_stat::MOVE_COST, translate_marker( "Move cost" ) ),
        std::make_pair( affected_stat::DAMAGE, translate_marker( "damage" ) ),
        std::make_pair( affected_stat::ARMOR, translate_marker( "Armor" ) ),
        std::make_pair( affected_stat::ARMOR_PENETRATION, translate_marker( "Armor penetration" ) ),
        std::make_pair( affected_stat::TARGET_ARMOR_MULTIPLIER, translate_marker( "Target armor multiplier" ) ),
    }
};

static std::string string_from_affected_stat( const affected_stat &s )
{
    const auto &iter = affected_stat_map_translation.find( s );
    return iter != affected_stat_map_translation.end() ? _( iter->second ) : "";
}

static const std::map<scaling_stat, std::string> scaling_stat_map_translation = {{
        std::make_pair( STAT_STR, translate_marker( "strength" ) ),
        std::make_pair( STAT_DEX, translate_marker( "dexterity" ) ),
        std::make_pair( STAT_INT, translate_marker( "intelligence" ) ),
        std::make_pair( STAT_PER, translate_marker( "perception" ) ),
    }
};

static std::string string_from_scaling_stat( const scaling_stat &s )
{
    const auto &iter = scaling_stat_map_translation.find( s );
    return iter != scaling_stat_map_translation.end() ? _( iter->second ) : "";
}

bonus_container::bonus_container() = default;

effect_scaling::effect_scaling( const JsonObject &obj )
{
    if( obj.has_string( "scaling-stat" ) ) {
        stat = scaling_stat_from_string( obj.get_string( "scaling-stat" ) );
    } else {
        stat = STAT_NULL;
    }

    scale = obj.get_float( "scale" );
}

void bonus_container::load( const JsonObject &jo )
{
    load( jo.get_array( "flat_bonuses" ), false );
    load( jo.get_array( "mult_bonuses" ), true );
}

void bonus_container::load( const JsonArray &jarr, const bool mult )
{
    for( const JsonObject qualifiers : jarr ) {
        const affected_stat as = affected_stat_from_string( qualifiers.get_string( "stat" ) );
        if( as == affected_stat::NONE ) {
            qualifiers.throw_error_at( "stat", "Invalid affected stat" );
        }

        damage_type_id dt;
        if( needs_damage_type( as ) ) {
            qualifiers.read( "type", dt );
            if( dt == damage_type_id::NULL_ID() ) {
                qualifiers.throw_error_at( "type", "Invalid damage type" );
            }
        }

        const affected_type at( as, dt );

        bonus_container::bonus_map &selected = mult ? bonuses_mult : bonuses_flat;
        selected[at].emplace_back( qualifiers );
    }
}

affected_type::affected_type( affected_stat s )
{
    stat = s;
}

affected_type::affected_type( affected_stat s, const damage_type_id &t )
{
    stat = s;
    if( needs_damage_type( s ) ) {
        type = t;
    } else {
        type = damage_type_id();
    }
}

bool affected_type::operator<( const affected_type &other ) const
{
    return stat < other.stat || ( stat == other.stat && type < other.type );
}
bool affected_type::operator==( const affected_type &other ) const
{
    // NOTE: Constructor has to ensure type is set to NULL for some stats
    return stat == other.stat && type == other.type;
}

float bonus_container::get_flat( const Character &u, affected_stat stat,
                                 const damage_type_id &dt ) const
{
    const affected_type type( stat, dt );
    const auto &iter = bonuses_flat.find( type );
    if( iter == bonuses_flat.end() ) {
        return 0.0f;
    }

    float ret = 0.0f;
    for( const effect_scaling &es : iter->second ) {
        ret += es.get( u );
    }

    return ret;
}
float bonus_container::get_flat( const Character &u, affected_stat stat ) const
{
    return get_flat( u, stat, damage_type_id() );
}

float bonus_container::get_mult( const Character &u, affected_stat stat,
                                 const damage_type_id &dt ) const
{
    const affected_type type( stat, dt );
    const auto &iter = bonuses_mult.find( type );
    if( iter == bonuses_mult.end() ) {
        return 1.0f;
    }

    float ret = 1.0f;
    for( const effect_scaling &es : iter->second ) {
        ret *= es.get( u );
    }

    // Currently all relevant effects require non-negative values
    return std::max( 0.0f, ret );
}
float bonus_container::get_mult( const Character &u, affected_stat stat ) const
{
    return get_mult( u, stat, damage_type_id() );
}

std::string bonus_container::get_description() const
{
    std::string dump;
    for( const auto &boni : bonuses_mult ) {
        std::string type = string_from_affected_stat( boni.first.get_stat() );

        if( needs_damage_type( boni.first.get_stat() ) ) {
            const damage_type_id &dt = boni.first.get_damage_type();
            //~ %1$s: damage type, %2$s: damage-related bonus name
            type = string_format( pgettext( "type of damage", "%1$s %2$s" ),
                                  dt.is_null() ? _( "none" ) : dt->name.translated(), type );
        }

        for( const effect_scaling &sf : boni.second ) {
            if( sf.stat ) {
                //~ %1$s: bonus name, %2$d: bonus percentage, %3$s: stat name
                dump += string_format( pgettext( "martial art bonus", "* %1$s: <stat>%2$d%%</stat> of %3$s" ),
                                       type, static_cast<int>( sf.scale * 100 ), string_from_scaling_stat( sf.stat ) );
            } else {
                //~ %1$s: bonus name, %2$d: bonus percentage
                dump += string_format( pgettext( "martial art bonus", "* %1$s: <stat>%2$d%%</stat>" ),
                                       type, static_cast<int>( sf.scale * 100 ) );
            }
            dump += "\n";
        }
    }

    for( const auto &boni : bonuses_flat ) {
        std::string type = string_from_affected_stat( boni.first.get_stat() );

        if( needs_damage_type( boni.first.get_stat() ) ) {
            const damage_type_id &dt = boni.first.get_damage_type();
            //~ %1$s: damage type, %2$s: damage-related bonus name
            type = string_format( pgettext( "type of damage", "%1$s %2$s" ),
                                  dt.is_null() ? _( "none" ) : dt->name.translated(), type );
        }

        for( const effect_scaling &sf : boni.second ) {
            if( sf.stat ) {
                //~ %1$s: bonus name, %2$+d: bonus percentage, %3$s: stat name
                dump += string_format( pgettext( "martial art bonus", "* %1$s: <stat>%2$+d%%</stat> of %3$s" ),
                                       type, static_cast<int>( sf.scale * 100 ), string_from_scaling_stat( sf.stat ) );
            } else {
                //~ %1$s: bonus name, %2$+d: bonus value
                dump += string_format( pgettext( "martial art bonus", "* %1$s: <stat>%2$+d</stat>" ),
                                       type, static_cast<int>( sf.scale ) );
            }
            dump += "\n";
        }
    }

    return dump;
}

float effect_scaling::get( const Character &u ) const
{
    float bonus = 0.0f;
    switch( stat ) {
        case STAT_STR:
            bonus = scale * u.get_str();
            break;
        case STAT_DEX:
            bonus = scale * u.get_dex();
            break;
        case STAT_INT:
            bonus = scale * u.get_int();
            break;
        case STAT_PER:
            bonus = scale * u.get_per();
            break;
        case STAT_NULL:
            bonus = scale;
        case NUM_STATS:
            break;
    }

    return bonus;
}