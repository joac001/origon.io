extends Node3D
class_name WeaponController

signal ammo_changed(current: int, reserve: int)
signal weapon_changed(weapon_name: String)

enum Slot { PRIMARY, SECONDARY, MELEE }

const RELOAD_CHECK_INTERVAL = 0.05

var slots: Dictionary = { Slot.PRIMARY: null, Slot.SECONDARY: null, Slot.MELEE: null }
var current_slot: Slot = Slot.PRIMARY
var current_weapon: WeaponResource = null

var mag_ammo: int = 0
var reserve_ammo: int = 0

var can_shoot: bool = true
var is_reloading: bool = false
var shoot_timer: float = 0.0
var reload_timer: float = 0.0

@onready var raycast: RayCast3D = $RayCast3D

func _ready() -> void:
	var assault_rifle = load("res://resources/weapons/assault_rifle.tres")
	var pistol = load("res://resources/weapons/pistol.tres")
	var knife = load("res://resources/weapons/knife_combat.tres")
	equip_weapon(assault_rifle, Slot.PRIMARY)
	equip_weapon(pistol, Slot.SECONDARY)
	equip_weapon(knife, Slot.MELEE)
	switch_slot(Slot.PRIMARY)

func _process(delta: float) -> void:
	if shoot_timer > 0.0:
		shoot_timer -= delta
		if shoot_timer <= 0.0:
			can_shoot = true

	if is_reloading:
		reload_timer -= delta
		if reload_timer <= 0.0:
			_finish_reload()

	if Input.is_action_pressed("shoot") and can_shoot and not is_reloading:
		shoot()
	if Input.is_action_just_pressed("reload") and not is_reloading:
		start_reload()
	if Input.is_action_just_pressed("weapon_primary"):
		switch_slot(Slot.PRIMARY)
	if Input.is_action_just_pressed("weapon_secondary"):
		switch_slot(Slot.SECONDARY)
	if Input.is_action_just_pressed("weapon_melee"):
		switch_slot(Slot.MELEE)

func equip_weapon(weapon: WeaponResource, slot: Slot) -> void:
	slots[slot] = weapon

func switch_slot(slot: Slot) -> void:
	if slots[slot] == null:
		return
	current_slot = slot
	current_weapon = slots[slot]
	is_reloading = false
	reload_timer = 0.0
	can_shoot = true
	shoot_timer = 0.0
	mag_ammo = current_weapon.mag_capacity
	reserve_ammo = current_weapon.ammo_reserve
	ammo_changed.emit(mag_ammo, reserve_ammo)
	weapon_changed.emit(current_weapon.weapon_name)

func shoot() -> void:
	if current_weapon == null:
		return
	if current_weapon.is_melee:
		_do_melee()
		return
	if mag_ammo <= 0:
		if reserve_ammo > 0:
			start_reload()
		return

	mag_ammo -= 1
	ammo_changed.emit(mag_ammo, reserve_ammo)

	can_shoot = false
	shoot_timer = current_weapon.fire_rate if current_weapon.fire_rate > 0 else 0.1

	if raycast.is_colliding():
		var hit = raycast.get_collider()
		if hit and hit.has_method("take_damage"):
			hit.take_damage(current_weapon.damage_tdm)

func _do_melee() -> void:
	can_shoot = false
	shoot_timer = 1.0
	if raycast.is_colliding():
		var hit = raycast.get_collider()
		if hit and hit.has_method("take_damage"):
			hit.take_damage(current_weapon.damage_tdm)

func start_reload() -> void:
	if current_weapon == null or current_weapon.is_melee:
		return
	if mag_ammo == current_weapon.mag_capacity or reserve_ammo <= 0:
		return
	is_reloading = true
	can_shoot = false
	reload_timer = current_weapon.reload_time

func _finish_reload() -> void:
	var needed = current_weapon.mag_capacity - mag_ammo
	var to_load = min(needed, reserve_ammo)
	mag_ammo += to_load
	reserve_ammo -= to_load
	is_reloading = false
	can_shoot = true
	ammo_changed.emit(mag_ammo, reserve_ammo)
