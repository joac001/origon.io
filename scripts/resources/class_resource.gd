extends Resource
class_name ClassResource

@export var class_name_str: String = "Unnamed"
@export var primary_weapon: WeaponResource
@export var secondary_weapon: WeaponResource
@export var melee_weapon: WeaponResource

func _to_string() -> String:
	return "[ClassResource: %s]" % class_name_str
