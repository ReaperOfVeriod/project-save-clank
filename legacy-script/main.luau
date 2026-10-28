--!strict
-- Experimental gameplay-field snapshot, not a full savestate.

local save_key = openrac.setting("save_key") :: string
local load_key = openrac.setting("load_key") :: string

type Snapshot = {
	position_x: number,
	position_y: number,
	position_z: number,
	facing: number,
	bolts: number,
}

local snapshot: Snapshot? = nil
local previous_save_down = false
local previous_load_down = false

openrac.on_frame(function()
	local save_down = openrac.key(save_key)
	local load_down = openrac.key(load_key)
	local save_pressed = save_down and not previous_save_down
	local load_pressed = load_down and not previous_load_down
	previous_save_down = save_down
	previous_load_down = load_down

	if save_pressed then
		snapshot = {
			position_x = openrac.fields.hero.position.x,
			position_y = openrac.fields.hero.position.y,
			position_z = openrac.fields.hero.position.z,
			facing = openrac.fields.hero.facing,
			bolts = openrac.fields.bolts,
		}
		openrac.log("Gameplay snapshot saved: hero position, facing, and bolts.")
	elseif load_pressed then
		local current = snapshot
		if current == nil then
			openrac.warn("No gameplay snapshot is available; press the save key first.")
			return
		end

		openrac.fields.hero.position.x = current.position_x
		openrac.fields.hero.position.y = current.position_y
		openrac.fields.hero.position.z = current.position_z
		openrac.fields.hero.facing = current.facing
		openrac.fields.bolts = current.bolts
		openrac.warn("Gameplay fields restored. World objects, animation, camera, health, audio, and port state were not restored.")
	end
end)
