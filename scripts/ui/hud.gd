extends CanvasLayer

@onready var health_bar: ProgressBar = $Root/BottomLeft/HealthRow/HealthBar
@onready var health_value: Label = $Root/BottomLeft/HealthRow/HealthBar/HealthValue
@onready var stamina_bar: ProgressBar = $Root/BottomLeft/StaminaRow/StaminaBar
@onready var ammo_label: Label = $Root/BottomRight/AmmoLabel
@onready var weapon_name_label: Label = $Root/BottomRight/WeaponName
@onready var crouch_indicator: Label = $Root/BottomLeft/CrouchIndicator

func _ready() -> void:
	add_to_group("hud")
	health_bar.max_value = 100
	health_bar.value = 100
	health_value.text = "100"
	stamina_bar.max_value = 100.0
	stamina_bar.value = 100.0
	ammo_label.text = "-- / --"
	weapon_name_label.text = ""
	crouch_indicator.text = ""

func update_health(value: int) -> void:
	health_bar.value = value
	health_value.text = str(value)

func update_stamina(value: float) -> void:
	stamina_bar.value = value

func update_crouch(is_crouching: bool) -> void:
	crouch_indicator.text = "[ CROUCHED ]" if is_crouching else ""

func update_ammo(current: int, reserve: int) -> void:
	ammo_label.text = "%d / %d" % [current, reserve]

func update_weapon_name(weapon_name: String) -> void:
	weapon_name_label.text = weapon_name
