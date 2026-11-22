-- zombie.lua -- ZOMBIE protocol dissector for Wireshark / tshark
--
-- Wire format (all little-endian), frozen by labs/07-net-os/BRIEF.md:
--
--   offset  size  field
--   0       2     total_len  = 1 (opcode) + 1 (flags) + payload length.
--                              It does NOT include these first 2 bytes.
--   2       1     opcode     0x01 HELLO / 0x02 ATTACK / 0x03 HP_SYNC
--                            0x04 SNAPSHOT / 0x05 BYE
--   3       1     flags      reserved, always 0
--   4       N     payload    (little-endian u32/f32/...)
--
-- Minimum frame is 4 bytes (total_len = 2, empty payload).  The TCP
-- dissector hands us the TCP payload only, so a single TCP segment may
-- carry several ZOMBIE frames or only a part of one (SNAPSHOT with
-- count=1000 is 16006 bytes).  We use TCP desegmentation for that.
--
-- Usage:
--   tshark -r demo.pcapng -X lua_script:zombie.lua -V
--   tshark -r demo.pcapng -X lua_script:zombie.lua -T fields -e zombie.opcode
--   Wireshark GUI: copy this file into the "Personal Lua Plugins" folder
--   (%APPDATA%\Wireshark\plugins) and restart Wireshark, or load it from
--   Tools -> Lua -> Evaluate (not needed for tshark).
--
-- Fields produced: zombie.*  (zombie.total_len, zombie.opcode, zombie.flags,
--   zombie.client_id, zombie.version, zombie.damage, zombie.seq,
--   zombie.claimed_hp, zombie.player_id, zombie.hp, zombie.max_hp,
--   zombie.tick, zombie.alive, zombie.pad, zombie.count,
--   zombie.snapshot.id, zombie.snapshot.x, zombie.snapshot.y,
--   zombie.snapshot.hp)

local OPCODES = {
    [0x01] = "HELLO",
    [0x02] = "ATTACK",
    [0x03] = "HP_SYNC",
    [0x04] = "SNAPSHOT",
    [0x05] = "BYE",
}

local p_zombie = Proto("zombie", "ZOMBIE Protocol")

local F = p_zombie.fields

F.total_len  = ProtoField.uint16("zombie.total_len",  "Total Length",   base.DEC)
F.opcode     = ProtoField.uint8 ("zombie.opcode",     "Opcode",         base.HEX, OPCODES)
F.flags      = ProtoField.uint8 ("zombie.flags",      "Flags",          base.HEX)

-- HELLO
F.client_id  = ProtoField.uint32("zombie.client_id",  "Client ID",      base.DEC)
F.version    = ProtoField.uint32("zombie.version",    "Version",        base.DEC)

-- ATTACK
F.damage     = ProtoField.float ("zombie.damage",     "Damage")
F.seq        = ProtoField.uint32("zombie.seq",        "Sequence",       base.DEC)
F.claimed_hp = ProtoField.float ("zombie.claimed_hp", "Claimed HP")

-- HP_SYNC
F.player_id  = ProtoField.uint32("zombie.player_id",  "Player ID",      base.DEC)
F.hp         = ProtoField.float ("zombie.hp",         "HP")
F.max_hp     = ProtoField.float ("zombie.max_hp",     "Max HP")
F.tick       = ProtoField.uint32("zombie.tick",       "Server Tick",    base.DEC)
F.alive      = ProtoField.uint8 ("zombie.alive",      "Alive",          base.DEC)
F.pad        = ProtoField.bytes ("zombie.pad",        "Padding")

-- SNAPSHOT
F.count      = ProtoField.uint16("zombie.count",      "Entity Count",   base.DEC)
F.snap_id    = ProtoField.uint32("zombie.snapshot.id", "Entity ID",     base.DEC)
F.snap_x     = ProtoField.float ("zombie.snapshot.x",  "X")
F.snap_y     = ProtoField.float ("zombie.snapshot.y",  "Y")
F.snap_hp    = ProtoField.float ("zombie.snapshot.hp", "HP")

-- Everything we could not interpret.
F.unknown    = ProtoField.bytes ("zombie.payload",    "Payload")

-- ---------------------------------------------------------------------------
-- One complete ZOMBIE frame.  tvb is already trimmed to total_len + 2 bytes.
-- Returns a short one-line description for the Info column.
-- ---------------------------------------------------------------------------
local function dissect_frame(tvb, tree)
    local total_len = tvb(0, 2):le_uint()
    local opcode    = tvb(2, 1):uint()
    local name      = OPCODES[opcode] or string.format("UNKNOWN(0x%02x)", opcode)
    local payload_len = total_len - 2
    local desc      = nil

    local t = tree:add(p_zombie, tvb(0, tvb:len()),
                       string.format("ZOMBIE %s, %d bytes on the wire", name, tvb:len()))
    t:add_le(F.total_len, tvb(0, 2))
    t:add(F.opcode, tvb(2, 1))
    t:add(F.flags, tvb(3, 1))

    if opcode == 0x01 then              -- HELLO  u32 client_id, u32 version
        if payload_len >= 8 then
            local client_id = tvb(4, 4):le_uint()
            local version   = tvb(8, 4):le_uint()
            t:add_le(F.client_id, tvb(4, 4))
            t:add_le(F.version,   tvb(8, 4))
            desc = string.format("HELLO client_id=%d version=%d", client_id, version)
        else
            t:add(F.unknown, tvb(4, tvb:len() - 4))
            desc = "HELLO (truncated payload)"
        end

    elseif opcode == 0x02 then          -- ATTACK f32 damage, u32 seq, f32 claimed_hp
        if payload_len >= 12 then
            local damage     = tvb(4, 4):le_float()
            local seq        = tvb(8, 4):le_uint()
            local claimed_hp = tvb(12, 4):le_float()
            t:add_le(F.damage,     tvb(4, 4))
            t:add_le(F.seq,        tvb(8, 4))
            t:add_le(F.claimed_hp, tvb(12, 4))
            desc = string.format("ATTACK damage=%.2f seq=%d claimed_hp=%.2f",
                                 damage, seq, claimed_hp)
        else
            t:add(F.unknown, tvb(4, tvb:len() - 4))
            desc = "ATTACK (truncated payload)"
        end

    elseif opcode == 0x03 then          -- HP_SYNC player_id, hp, max_hp, tick, alive, pad[3]
        if payload_len >= 20 then
            local player_id = tvb(4, 4):le_uint()
            local hp        = tvb(8, 4):le_float()
            local max_hp    = tvb(12, 4):le_float()
            local tick      = tvb(16, 4):le_uint()
            local alive     = tvb(20, 1):uint()
            t:add_le(F.player_id, tvb(4, 4))
            t:add_le(F.hp,        tvb(8, 4))
            t:add_le(F.max_hp,    tvb(12, 4))
            t:add_le(F.tick,      tvb(16, 4))
            t:add(F.alive,        tvb(20, 1))
            t:add(F.pad,          tvb(21, 3))
            desc = string.format("HP_SYNC player_id=%d hp=%.2f/%.2f tick=%d alive=%d",
                                 player_id, hp, max_hp, tick, alive)
        else
            t:add(F.unknown, tvb(4, tvb:len() - 4))
            desc = "HP_SYNC (truncated payload)"
        end

    elseif opcode == 0x04 then          -- SNAPSHOT u16 count, count * {u32 id, f32 x, f32 y, f32 hp}
        if payload_len >= 2 then
            local count = tvb(4, 2):le_uint()
            t:add_le(F.count, tvb(4, 2))
            local need = 2 + count * 16
            local avail = payload_len
            local n = count
            if need > avail then
                n = math.floor((avail - 2) / 16)   -- dissect what we actually have
            end
            for i = 0, n - 1 do
                local off = 6 + i * 16
                local et = t:add(p_zombie, tvb(off, 16),
                                 string.format("Entity[%d]", i))
                et:add_le(F.snap_id, tvb(off, 4))
                et:add_le(F.snap_x,  tvb(off + 4, 4))
                et:add_le(F.snap_y,  tvb(off + 8, 4))
                et:add_le(F.snap_hp, tvb(off + 12, 4))
            end
            if n < count then
                desc = string.format("SNAPSHOT count=%d (only %d entities present)", count, n)
            else
                desc = string.format("SNAPSHOT count=%d", count)
            end
        else
            t:add(F.unknown, tvb(4, tvb:len() - 4))
            desc = "SNAPSHOT (truncated payload)"
        end

    elseif opcode == 0x05 then          -- BYE, empty payload
        desc = "BYE"

    else
        if payload_len > 0 then
            t:add(F.unknown, tvb(4, payload_len))
        end
        desc = string.format("%s payload=%d bytes", name, payload_len)
    end

    return desc
end

-- ---------------------------------------------------------------------------
-- TCP payload entry point.  May contain 0..n complete frames, plus possibly
-- a partial one at the end; ask the TCP dissector for the missing bytes.
-- ---------------------------------------------------------------------------
function p_zombie.dissector(buffer, pinfo, tree)
    local len = buffer:len()
    if len == 0 then
        return 0
    end

    pinfo.cols.protocol:set("ZOMBIE")

    local offset = 0
    while offset < len do
        local rem = len - offset

        -- Need the 4-byte frame header first.
        if rem < 4 then
            pinfo.desegment_offset = offset
            pinfo.desegment_len = 4 - rem
            return
        end

        local total_len = buffer(offset, 2):le_uint()
        local frame_len = total_len + 2

        if total_len < 2 then
            -- Corrupt / not our protocol: show the rest as data and stop.
            tree:add(F.unknown, buffer(offset, rem)):append_text(
                string.format(" (invalid total_len=%d, giving up)", total_len))
            return len
        end

        if frame_len > rem then
            -- Partial frame at the end of this segment: ask for more bytes.
            pinfo.desegment_offset = offset
            pinfo.desegment_len = frame_len - rem
            return
        end

        local desc = dissect_frame(buffer(offset, frame_len), tree)
        if desc then
            pinfo.cols.info:set(desc)
        end
        offset = offset + frame_len
    end

    return len
end

-- client -> tap on 47000, tap -> server on 47001 (the lab's real ports).
-- 4000/4001 stay registered for demo.pcapng, which was made before the port switch.
local tcp_port_table = DissectorTable.get("tcp.port")
tcp_port_table:add(47000, p_zombie)
tcp_port_table:add(47001, p_zombie)
tcp_port_table:add(4000, p_zombie)
tcp_port_table:add(4001, p_zombie)
