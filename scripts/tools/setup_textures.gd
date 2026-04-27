# Script to run in Godot editor console to apply textures to all props
# Usage: Open Godot, go to View > Toggle Bottom Panel > "Debugger" tab
# Paste this code in the "GDScript" console and run it

extends Node

func _ready():
	apply_textures_to_building_props()
	apply_textures_to_cars()
	apply_textures_to_city()
	print("Textures applied!")

func apply_textures_to_building_props():
	var texture = load("res://assets/models/building/Textures/colormap.png")
	var props = [
		"column", "column-thin", "column-wide",
		"wall", "wall-low", "wall-corner", "wall-half",
		"stairs-open", "stairs-closed", "stairs-center", "stairs-sides",
		"door-rotate-square-a", "door-rotate-round-a",
		"roof-flat-center", "floor", "barricade-window-a"
	]

	for prop_name in props:
		var scene_path = "res://scenes/props/%s.tscn" % prop_name
		if ResourceLoader.exists(scene_path):
			var scene = load(scene_path)
			var instance = scene.instantiate()
			apply_material_to_meshes(instance, texture)
			get_tree().root.add_child(instance)
			instance.queue_free()

func apply_textures_to_cars():
	var texture = load("res://assets/models/cars/Textures/colormap.png")
	var cars = ["car_sedan", "car_suv", "car_police"]

	for car_name in cars:
		var scene_path = "res://scenes/props/%s.tscn" % car_name
		if ResourceLoader.exists(scene_path):
			var scene = load(scene_path)
			var instance = scene.instantiate()
			apply_material_to_meshes(instance, texture)
			get_tree().root.add_child(instance)
			instance.queue_free()

func apply_textures_to_city():
	var texture = load("res://assets/models/city/Textures/colormap.png")
	var buildings = ["building-a", "building-b", "building-c"]

	for building_name in buildings:
		var scene_path = "res://scenes/props/%s.tscn" % building_name
		if ResourceLoader.exists(scene_path):
			var scene = load(scene_path)
			var instance = scene.instantiate()
			apply_material_to_meshes(instance, texture)
			get_tree().root.add_child(instance)
			instance.queue_free()

func apply_material_to_meshes(node: Node, texture: Texture2D):
	"""Recursively apply material with texture to all MeshInstance3D nodes."""
	if node is MeshInstance3D:
		var material = StandardMaterial3D.new()
		material.albedo_texture = texture
		node.set_surface_override_material(0, material)

	for child in node.get_children():
		apply_material_to_meshes(child, texture)
