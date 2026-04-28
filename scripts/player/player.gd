extends CharacterBody3D

signal health_changed(value: int)
signal ammo_changed(current: int, reserve: int)
signal stamina_changed(value: float)
signal crouching_changed(is_crouching: bool)

const WALK_SPEED = 4.0
const SPRINT_SPEED = 8.0
const CROUCH_SPEED = 2.5
const JUMP_VELOCITY = 5.0
const MOUSE_SENSITIVITY = 0.002
const GRAVITY_MULTIPLIER = 1.8
const STAMINA_MAX = 100.0
const STAMINA_DRAIN_RATE = 30.0
const STAMINA_REGEN_RATE = 20.0

var max_health = 100
var current_health = 100
var current_ammo = 30
var reserve_ammo = 120
var stamina = STAMINA_MAX
var stamina_exhausted = false
var is_crouching = false
var crouch_height = 0.7
var normal_height = 1.2

@onready var head: Node3D = $Head
@onready var collision_shape: CollisionShape3D = $CollisionShape3D
@onready var weapon_controller: WeaponController = $Head/WeaponController

func _ready() -> void:
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
	floor_snap_length = 0.5
	_set_body_layer($Body, 2)
	call_deferred("_connect_hud")

func _set_body_layer(node: Node, layer: int) -> void:
	if node is VisualInstance3D:
		node.visibility_layers = layer
	for child in node.get_children():
		_set_body_layer(child, layer)

func _connect_hud() -> void:
	var hud = get_tree().get_first_node_in_group("hud")
	if hud == null:
		push_warning("HUD not found in group 'hud'")
		return
	stamina_changed.connect(hud.update_stamina)
	crouching_changed.connect(hud.update_crouch)
	health_changed.connect(hud.update_health)
	weapon_controller.ammo_changed.connect(hud.update_ammo)
	weapon_controller.weapon_changed.connect(hud.update_weapon_name)
	# Emit initial values so HUD shows correct state from start
	health_changed.emit(current_health)
	weapon_controller.ammo_changed.emit(weapon_controller.mag_ammo, weapon_controller.reserve_ammo)
	weapon_controller.weapon_changed.emit(weapon_controller.current_weapon.weapon_name if weapon_controller.current_weapon else "")

func take_damage(amount: int) -> void:
	current_health = max(0, current_health - amount)
	health_changed.emit(current_health)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		rotate_y(-event.relative.x * MOUSE_SENSITIVITY)
		head.rotate_x(-event.relative.y * MOUSE_SENSITIVITY)
		head.rotation.x = clamp(head.rotation.x, -PI / 2.0, PI / 2.0)
	if event.is_action_pressed("ui_cancel"):
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)

func _physics_process(delta: float) -> void:
	if not is_on_floor():
		velocity += get_gravity() * GRAVITY_MULTIPLIER * delta

	if Input.is_action_just_pressed("jump") and is_on_floor():
		velocity.y = JUMP_VELOCITY

	if Input.is_action_just_pressed("crouch"):
		is_crouching = !is_crouching
		var capsule = collision_shape.shape as CapsuleShape3D
		if is_crouching:
			capsule.height = crouch_height
			collision_shape.position.y = 0.35
		else:
			capsule.height = normal_height
			collision_shape.position.y = 0.6
		crouching_changed.emit(is_crouching)

	if stamina <= 0.0:
		stamina_exhausted = true
	elif stamina >= STAMINA_MAX * 0.3:
		stamina_exhausted = false

	var is_sprinting = Input.is_action_pressed("sprint") and not is_crouching and not stamina_exhausted
	var speed = CROUCH_SPEED if is_crouching else (SPRINT_SPEED if is_sprinting else WALK_SPEED)
	var input_dir := Input.get_vector("move_left", "move_right", "move_forward", "move_backward")
	var direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()

	if is_sprinting:
		stamina = move_toward(stamina, 0.0, STAMINA_DRAIN_RATE * delta)
	else:
		stamina = move_toward(stamina, STAMINA_MAX, STAMINA_REGEN_RATE * delta)
	stamina_changed.emit(stamina)

	if is_on_floor():
		if direction:
			velocity.x = direction.x * speed
			velocity.z = direction.z * speed
		else:
			velocity.x = move_toward(velocity.x, 0, speed)
			velocity.z = move_toward(velocity.z, 0, speed)
	else:
		if direction:
			velocity.x = lerp(velocity.x, direction.x * speed * 0.5, 0.05)
			velocity.z = lerp(velocity.z, direction.z * speed * 0.5, 0.05)

	move_and_slide()
