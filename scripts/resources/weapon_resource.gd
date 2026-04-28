extends Resource
class_name WeaponResource

enum WeaponType { PRIMARY, SECONDARY, MELEE }

@export var weapon_name: String = "Unnamed"
@export var weapon_type: WeaponType = WeaponType.PRIMARY
@export var damage_tdm: int = 20
@export var damage_gungame: int = 20
@export var fire_rate: float = 0.1
@export var reload_time: float = 2.0
@export var mag_capacity: int = 30
@export var ammo_reserve: int = 120
@export var recoil: float = 1.0
@export var spread: float = 0.1
@export var is_melee: bool = false

func _to_string() -> String:
	return "[WeaponResource: %s (%s)]" % [weapon_name, WeaponType.keys()[weapon_type]]
