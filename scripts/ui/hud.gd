extends CanvasLayer

@onready var health_bar = $VBoxContainer/HealthBar
@onready var stamina_bar = $VBoxContainer/StaminaBar
@onready var ammo_label = $VBoxContainer/AmmoLabel
@onready var crouch_indicator = $VBoxContainer/CrouchIndicator
@onready var reticle = $VBoxContainer/Reticle

func _ready() -> void:
	health_bar.max_value = 100
	health_bar.value = 100
	stamina_bar.max_value = 100.0
	stamina_bar.value = 100.0
	ammo_label.text = "-- / --"
	crouch_indicator.text = ""
	reticle.modulate = Color.WHITE

func update_health(value: int) -> void:
	health_bar.value = value

func update_stamina(value: float) -> void:
	stamina_bar.value = value

func update_crouch(is_crouching: bool) -> void:
	crouch_indicator.text = "[CROUCH]" if is_crouching else ""

func update_ammo(current: int, reserve: int) -> void:
	ammo_label.text = "%d / %d" % [current, reserve]
