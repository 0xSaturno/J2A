<img src="https://i.imgur.com/NHL8p8L.png" title="" alt="J2A Banner" data-align="center">

# J2A (JSON to Asset) Plugin for Unreal Engine 5

J2A is an Editor utility plugin designed to seamlessly import raw FModel JSON exports directly into Unreal Engine 5 as template assets. 

With a focus on preserving hierarchies and visual data, it automates the tedious reconstruction of materials, instances, and data tables by translating raw JSON properties into their native UE counterparts.

## Features

- **Drag-and-Drop Workflow**: Simply drag and drop FModel JSON files directly into the Content Browser, and J2A will parse and build the appropriate assets automatically.
- **Context Menu Integration**: Right-click anywhere in the Content Browser and select "Import FModel JSON..." to import files into the active folder.
- **Auto-Generating Master Materials**: Missing Master Material dependencies? If an imported Material Instance points to an unloaded Master Material, J2A dynamically recreates it, populating it with all the required dummy Parameter Nodes (Scalars, Vectors, Textures) and their default values.
- **Auto-Spawning Dummy Textures**: Unresolved texture parameters are automatically handled. J2A creates blank dummy textures in your project tree to ensure your instances remain intact and linked correctly.
- **Robust Asset Support**: Fully supports importing:
  - `Material Instances`
  - `Master Materials` (Extracts `CachedExpressionData` to build functional node graphs)
  - `DataTables`
  - `DataAssets`

## Installation

1. Download the latest compiled release zip from the **Releases** section.
2. Extract the `J2A_Plugin` folder directly into your project's `Plugins/` directory (create the folder if it doesn't exist).
3. Launch Unreal Engine and ensure the "J2A" plugin is enabled in your `Edit -> Plugins` menu.

## Usage

**Method A: Drag & Drop**

1. Select one or more `.json` files exported via FModel.
2. Drag them directly into an open folder in the Unreal Engine Content Browser.

**Method B: Context Menu**

1. Right-click inside any Content Browser folder.
2. Select `JSON 2 Asset -> Import FModel JSON...`
3. Select your `.json` file(s) from the dialog box.

## Acknowledgments

This C++ plugin is a native, optimized port of the **[JSON2DA](https://github.com/peek6/Json2DA/tree/tekken8)** Python script toolset.

A massive thanks to **peek6** for their work on the Tekken 8 branch, and to **Tangerie** for the original JSON2DA project.
