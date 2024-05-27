/**************************************************************************/
/*  mesh_library_editor_plugin.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "mesh_library_editor_plugin.h"

#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_settings.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector_dock.h"
#include "editor/plugins/node_3d_editor_plugin.h"
#include "main/main.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/navigation_region_3d.h"
#include "scene/3d/physics/physics_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/gui/menu_button.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"

void MeshLibraryEditor::edit(const Ref<MeshLibrary> &p_mesh_library) {
	mesh_library = p_mesh_library;
	if (mesh_library.is_valid()) {
		menu->get_popup()->set_item_disabled(menu->get_popup()->get_item_index(MENU_OPTION_UPDATE_FROM_SCENE), !mesh_library->has_meta("_editor_source_scene"));
	}
}

void MeshLibraryEditor::_menu_remove_confirm() {
	switch (option) {
		case MENU_OPTION_REMOVE_ITEM: {
			mesh_library->remove_item(to_erase);
		} break;
		default: {
		};
	}
}

void MeshLibraryEditor::_menu_update_confirm(bool p_apply_xforms) {
	cd_update->hide();
	apply_xforms = p_apply_xforms;
	String existing = mesh_library->get_meta("_editor_source_scene");
	ERR_FAIL_COND(existing.is_empty());
	_import_scene_cbk(existing);
}

void MeshLibraryEditor::_import_scene(Node *p_scene, Ref<MeshLibrary> p_library, bool p_merge, bool p_apply_xforms) {
	if (!p_merge) {
		p_library->clear();
	}

	HashMap<int, MeshInstance3D *> mesh_instances;

	for (int i = 0; i < p_scene->get_child_count(); i++) {
		_import_scene_parse_node(p_library, mesh_instances, p_scene->get_child(i), p_merge, p_apply_xforms);
	}

	//generate previews!

	if (true) {
		Vector<Ref<Mesh>> meshes;
		Vector<Transform3D> transforms;
		Vector<int> ids = p_library->get_item_list();
		for (int i = 0; i < ids.size(); i++) {
			if (mesh_instances.find(ids[i])) {
				meshes.push_back(p_library->get_item(ids[i])->get_mesh());
				transforms.push_back(mesh_instances[ids[i]]->get_transform());
			}
		}

		Vector<Ref<Texture2D>> textures = EditorInterface::get_singleton()->make_mesh_previews(meshes, &transforms, EDITOR_GET("editors/grid_map/preview_size"));
		int j = 0;
		for (int i = 0; i < ids.size(); i++) {
			if (mesh_instances.find(ids[i])) {
				p_library->get_item(ids[i])->set_preview(textures[j]);
				j++;
			}
		}
	}
}

void MeshLibraryEditor::_import_scene_cbk(const String &p_str) {
	Ref<PackedScene> ps = ResourceLoader::load(p_str, "PackedScene");
	ERR_FAIL_COND(ps.is_null());
	Node *scene = ps->instantiate();

	ERR_FAIL_NULL_MSG(scene, "Cannot create an instance from PackedScene '" + p_str + "'.");

	_import_scene(scene, mesh_library, option == MENU_OPTION_UPDATE_FROM_SCENE, apply_xforms);

	memdelete(scene);
	mesh_library->set_meta("_editor_source_scene", p_str);

	menu->get_popup()->set_item_disabled(menu->get_popup()->get_item_index(MENU_OPTION_UPDATE_FROM_SCENE), false);
}

void MeshLibraryEditor::_import_scene_parse_node(Ref<MeshLibrary> p_library, HashMap<int, MeshInstance3D *> &p_mesh_instances, Node *p_node, bool p_merge, bool p_apply_xforms) {
	MeshInstance3D *mesh_instance_node = Object::cast_to<MeshInstance3D>(p_node);

	if (!mesh_instance_node) {
		// No MeshInstance so search deeper ...
		for (int i = 0; i < p_node->get_child_count(); i++) {
			_import_scene_parse_node(p_library, p_mesh_instances, p_node->get_child(i), p_merge, p_apply_xforms);
		}
		return;
	}

	Ref<Mesh> source_mesh = mesh_instance_node->get_mesh();
	if (source_mesh.is_null()) {
		return;
	}

	int item_id = p_library->find_item_by_name(mesh_instance_node->get_name());
	if (item_id < 0) {
		item_id = p_library->get_last_unused_item_id();
		p_library->create_item(item_id);
		p_library->get_item(item_id)->set_name(mesh_instance_node->get_name());
	} else if (!p_merge) {
		WARN_PRINT(vformat("MeshLibrary export found a MeshInstance3D with a duplicated name '%s' in the exported scene that overrides a previously parsed MeshInstance3D item with the same name.", mesh_instance_node->get_name()));
	}
	p_mesh_instances[item_id] = mesh_instance_node;

	Ref<Mesh> item_mesh = source_mesh->duplicate();
	for (int i = 0; i < item_mesh->get_surface_count(); i++) {
		Ref<Material> surface_override_material = mesh_instance_node->get_surface_override_material(i);
		if (surface_override_material.is_valid()) {
			item_mesh->surface_set_material(i, surface_override_material);
		}
	}
	p_library->get_item(item_id)->set_mesh(item_mesh);

	Transform3D item_mesh_transform;
	if (p_apply_xforms) {
		item_mesh_transform = mesh_instance_node->get_transform();
	}
	p_library->get_item(item_id)->set_mesh_transform(item_mesh_transform);

	Vector<Item::ShapeData> collisions;
	for (int i = 0; i < mesh_instance_node->get_child_count(); i++) {
		StaticBody3D *static_body_node = Object::cast_to<StaticBody3D>(mesh_instance_node->get_child(i));
		if (!static_body_node) {
			continue;
		}
		List<uint32_t> shapes;
		static_body_node->get_shape_owners(&shapes);
		for (uint32_t &E : shapes) {
			if (static_body_node->is_shape_owner_disabled(E)) {
				continue;
			}
			Transform3D shape_transform;
			if (p_apply_xforms) {
				shape_transform = mesh_instance_node->get_transform();
			}
			shape_transform *= static_body_node->get_transform() * static_body_node->shape_owner_get_transform(E);
			for (int k = 0; k < static_body_node->shape_owner_get_shape_count(E); k++) {
				Ref<Shape3D> collision_shape = static_body_node->shape_owner_get_shape(E, k);
				if (!collision_shape.is_valid()) {
					continue;
				}
				Item::ShapeData shape_data;
				shape_data.shape = collision_shape;
				shape_data.local_transform = shape_transform;
				collisions.push_back(shape_data);
			}
		}
	}
	p_library->get_item(item_id)->set_shapes(collisions);

	for (int i = 0; i < mesh_instance_node->get_child_count(); i++) {
		NavigationRegion3D *navigation_region_node = Object::cast_to<NavigationRegion3D>(mesh_instance_node->get_child(i));
		if (!navigation_region_node) {
			continue;
		}
		Ref<NavigationMesh> navigation_mesh = navigation_region_node->get_navigation_mesh();
		if (!navigation_mesh.is_null()) {
			Transform3D navigation_mesh_transform = navigation_region_node->get_transform();
			p_library->get_item(item_id)->set_navigation_mesh(navigation_mesh);
			p_library->get_item(item_id)->set_navigation_mesh_transform(navigation_mesh_transform);
			break;
		}
	}
}

Error MeshLibraryEditor::update_library_file(Node *p_base_scene, Ref<MeshLibrary> ml, bool p_merge, bool p_apply_xforms) {
	_import_scene(p_base_scene, ml, p_merge, p_apply_xforms);
	return OK;
}

void MeshLibraryEditor::_menu_cbk(int p_option) {
	option = p_option;
	switch (p_option) {
		case MENU_OPTION_ADD_ITEM: {
			mesh_library->create_item(mesh_library->get_last_unused_item_id());
		} break;
		case MENU_OPTION_REMOVE_ITEM: {
			String p = InspectorDock::get_inspector_singleton()->get_selected_path();
			if (p.begins_with("item") && p.get_slice_count("/") >= 2) {
				to_erase = p.get_slice("/", 1).to_int();
				cd_remove->set_text(vformat(TTR("Remove item %d?"), to_erase));
				cd_remove->popup_centered(Size2(300, 60));
			}
		} break;
		case MENU_OPTION_IMPORT_FROM_SCENE: {
			apply_xforms = false;
			file->popup_file_dialog();
		} break;
		case MENU_OPTION_IMPORT_FROM_SCENE_APPLY_XFORMS: {
			apply_xforms = true;
			file->popup_file_dialog();
		} break;
		case MENU_OPTION_UPDATE_FROM_SCENE: {
			cd_update->set_text(vformat(TTR("Update from existing scene?:\n%s"), String(mesh_library->get_meta("_editor_source_scene"))));
			cd_update->popup_centered(Size2(500, 60));
		} break;
	}
}

void MeshLibraryEditor::_move_mesh_library_array_element(Object *p_undo_redo, Object *p_edited, const String &p_array_prefix, int p_from_index, int p_to_pos) {
	EditorUndoRedoManager *undo_redo_man = Object::cast_to<EditorUndoRedoManager>(p_undo_redo);
	ERR_FAIL_NULL(undo_redo_man);

	MeshLibrary *ed_mesh_lib = Object::cast_to<MeshLibrary>(p_edited);
	if (!ed_mesh_lib) {
		return;
	}

	Vector<String> components = String(p_array_prefix).split("/", true, 2);

	// Compute the array indices to save.
	int begin = 0;
	int end;
	if (p_array_prefix == "custom_data_layer_") {
		end = ed_mesh_lib->get_custom_data_layers_count();
	} else {
		ERR_FAIL_MSG("Invalid array prefix for TileSet.");
	}

	if (p_from_index < 0) {
		// Adding new.
		if (p_to_pos >= 0) {
			begin = p_to_pos;
		} else {
			end = 0; // Nothing to save when adding at the end.
		}
	} else if (p_to_pos < 0) {
		// Removing.
		begin = p_from_index;
	} else {
		// Moving.
		begin = MIN(p_from_index, p_to_pos);
		end = MIN(MAX(p_from_index, p_to_pos) + 1, end);
	}

#define ADD_UNDO(obj, property) undo_redo_man->add_undo_property(obj, property, obj->get(property));

	if (p_array_prefix == "custom_data_layer_") {
		if (p_from_index < 0) {
			undo_redo_man->add_undo_method(ed_mesh_lib, "remove_custom_data_layer", p_to_pos < 0 ? ed_mesh_lib->get_custom_data_layers_count() : p_to_pos);
		}
	}

	// Save layers' properties.
	List<PropertyInfo> properties;
	ed_mesh_lib->get_property_list(&properties);
	for (PropertyInfo pi : properties) {
		if (pi.name.begins_with(p_array_prefix)) {
			String str = pi.name.trim_prefix(p_array_prefix);
			int to_char_index = 0;
			while (to_char_index < str.length()) {
				if (!is_digit(str[to_char_index])) {
					break;
				}
				to_char_index++;
			}
			if (to_char_index > 0) {
				int array_index = str.left(to_char_index).to_int();
				if (array_index >= begin && array_index < end) {
					ADD_UNDO(ed_mesh_lib, pi.name);
				}
			}
		}
	}

	if (p_array_prefix == "custom_data_layer_") {
		for (int layer_index = begin; layer_index < end; layer_index++) {
			// Loop through all the meshes in the library
			for (int i = 0; i < mesh_library->get_item_count(); i++) {
				int item_id = mesh_library->get_item_list()[i];
				Item *item = mesh_library->get_item(item_id);
				ERR_FAIL_NULL(item);
				ADD_UNDO(item, vformat("custom_data_%d", layer_index));
			}
		}
	}

#undef ADD_UNDO

	if (p_array_prefix == "custom_data_layer_") {
		if (p_from_index < 0) {
			undo_redo_man->add_do_method(ed_mesh_lib, "add_custom_data_layer", p_to_pos);
		} else if (p_to_pos < 0) {
			undo_redo_man->add_do_method(ed_mesh_lib, "remove_custom_data_layer", p_from_index);
		} else {
			undo_redo_man->add_do_method(ed_mesh_lib, "move_custom_data_layer", p_from_index, p_to_pos);
		}
	}
}

void MeshLibraryEditor::_bind_methods() {
}

MeshLibraryEditor::MeshLibraryEditor() {
	file = memnew(EditorFileDialog);
	file->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	//not for now?
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("PackedScene", &extensions);
	file->clear_filters();
	file->set_title(TTR("Import Scene"));
	for (const String &extension : extensions) {
		file->add_filter("*." + extension, extension.to_upper());
	}
	add_child(file);
	file->connect("file_selected", callable_mp(this, &MeshLibraryEditor::_import_scene_cbk));

	menu = memnew(MenuButton);
	Node3DEditor::get_singleton()->add_control_to_menu_panel(menu);
	menu->set_position(Point2(1, 1));
	menu->set_text(TTR("MeshLibrary"));
	menu->set_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("MeshLibrary"), EditorStringName(EditorIcons)));
	menu->get_popup()->add_item(TTR("Add Item"), MENU_OPTION_ADD_ITEM);
	menu->get_popup()->add_item(TTR("Remove Selected Item"), MENU_OPTION_REMOVE_ITEM);
	menu->get_popup()->add_separator();
	menu->get_popup()->add_item(TTR("Import from Scene (Ignore Transforms)"), MENU_OPTION_IMPORT_FROM_SCENE);
	menu->get_popup()->add_item(TTR("Import from Scene (Apply Transforms)"), MENU_OPTION_IMPORT_FROM_SCENE_APPLY_XFORMS);
	menu->get_popup()->add_item(TTR("Update from Scene"), MENU_OPTION_UPDATE_FROM_SCENE);
	menu->get_popup()->set_item_disabled(menu->get_popup()->get_item_index(MENU_OPTION_UPDATE_FROM_SCENE), true);
	menu->get_popup()->connect("id_pressed", callable_mp(this, &MeshLibraryEditor::_menu_cbk));
	menu->hide();

	cd_remove = memnew(ConfirmationDialog);
	add_child(cd_remove);
	cd_remove->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &MeshLibraryEditor::_menu_remove_confirm));
	cd_update = memnew(ConfirmationDialog);
	add_child(cd_update);
	cd_update->set_ok_button_text(TTR("Apply without Transforms"));
	cd_update->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &MeshLibraryEditor::_menu_update_confirm).bind(false));
	cd_update->add_button(TTR("Apply with Transforms"))->connect(SceneStringName(pressed), callable_mp(this, &MeshLibraryEditor::_menu_update_confirm).bind(true));

	// changes
	EditorNode::get_editor_data().add_move_array_element_function(SNAME("MeshLibrary"), callable_mp(this, &MeshLibraryEditor::_move_mesh_library_array_element));
}

void MeshLibraryEditorPlugin::edit(Object *p_node) {
	if (Object::cast_to<MeshLibrary>(p_node)) {
		mesh_library_editor->edit(Object::cast_to<MeshLibrary>(p_node));
		mesh_library_editor->show();
	} else {
		mesh_library_editor->edit(Ref<MeshLibrary>());
		mesh_library_editor->hide();
	}
}

bool MeshLibraryEditorPlugin::handles(Object *p_node) const {
	return p_node->is_class("MeshLibrary");
}

void MeshLibraryEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		mesh_library_editor->show();
		mesh_library_editor->get_menu_button()->show();
	} else {
		mesh_library_editor->hide();
		mesh_library_editor->get_menu_button()->hide();
	}
}

MeshLibraryEditorPlugin::MeshLibraryEditorPlugin() {
	mesh_library_editor = memnew(MeshLibraryEditor);

	EditorNode::get_singleton()->get_main_screen_control()->add_child(mesh_library_editor);
	mesh_library_editor->set_anchors_and_offsets_preset(Control::PRESET_TOP_WIDE);
	mesh_library_editor->set_end(Point2(0, 22));
	mesh_library_editor->hide();
}
