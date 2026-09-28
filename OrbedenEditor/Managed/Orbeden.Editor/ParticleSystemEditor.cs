using System.Reflection;
using Orbeden;

namespace OrbedenEditor;

/// <summary>粒子发射器的检视面板：模块配置、曲线与渐变、编辑态预览。</summary>
[CustomEditor(typeof(Orbeden.ParticleSystem))]
internal sealed class ParticleSystemEditor : ComponentEditor
{
    //最近一次读取或提交失败的原因，显示在面板底部
    private string lastError = string.Empty;
    private ParticleSettings working;
    //打开这一帧时第一个目标的配置，提交时用它判断用户改动了哪些叶字段
    private ParticleSettings baseline;
    private bool settingsLoaded;
    private bool settingsMixed;

    public override void OnDrawInspector()
    {
        lastError = string.Empty;
        settingsLoaded = TryLoadSettings();

        DrawStatus();
        DrawPreviewControls();
        if (!settingsLoaded)
        {
            EditorGUI.TextWrapped("Cannot read the particle settings of this component.");
            DrawDefaultInspector();
            return;
        }

        if (settingsMixed) EditorGUI.TextWrapped("Multi-selection has different particle settings; a change is written to every selected emitter, the fields you did not touch are left alone.");

        bool changed = false;
        changed |= DrawMain();
        changed |= DrawEmission();
        changed |= DrawShape();
        changed |= DrawMotion();
        changed |= DrawCollision();
        changed |= DrawTrails();
        changed |= DrawRendering();
        changed |= DrawSubEmitters();

        if (changed) Commit();
        if (lastError.Length != 0) EditorGUI.TextColored(lastError, new color { r = 1.0f, g = 0.5f, b = 0.35f, a = 1.0f });
    }

    //读取第一个目标的配置；各目标不一致时标记为混合
    private bool TryLoadSettings()
    {
        settingsMixed = false;
        ParticleSettings? first = null;
        foreach (IPropertyTarget target in Properties.Targets)
        {
            if (!TryReadTargetSettings(target, out ParticleSettings settings)) return false;
            if (first == null) first = settings;
            else if (!SettingsEqual(first.Value, settings)) settingsMixed = true;
        }

        if (first == null) return false;
        working = first.Value;
        baseline = first.Value;
        return true;
    }

    private static bool TryReadTargetSettings(IPropertyTarget target, out ParticleSettings settings)
    {
        settings = default;
        if (target.TryGet("settings", out InteropValue value) != InteropStatus.Ok) return false;
        ParticleSettingsParseResult parsed = Orbeden.ParticleSystem.ParseSettings(value.Value as string ?? string.Empty);
        if (!parsed.success) return false;
        settings = parsed.settings;
        return true;
    }

    //比较时走规范化文本，避免逐字段展开
    private static bool SettingsEqual(ParticleSettings left, ParticleSettings right)
    {
        return string.Equals(Orbeden.ParticleSystem.FormatSettings(left), Orbeden.ParticleSystem.FormatSettings(right), StringComparison.Ordinal);
    }

    //提交：每个目标读自己的文本、改用户正在编辑的叶字段、再写回
    private void Commit()
    {
        List<PropertyTargetWrite> writes = [];
        foreach (IPropertyTarget target in Properties.Targets)
        {
            if (!TryReadTargetSettings(target, out ParticleSettings settings))
            {
                lastError = "The particle settings of a selected emitter cannot be read.";
                return;
            }

            //只搬用户改过的叶字段：整段写回会用第一个目标的配置冲掉其它目标的未编辑字段
            object boxed = settings;
            ApplyEditedFields(baseline, working, boxed);
            string candidate = Orbeden.ParticleSystem.FormatSettings((ParticleSettings)boxed);
            if (candidate.Length == 0)
            {
                lastError = "The particle settings are invalid; the native configuration is unchanged.";
                return;
            }

            writes.Add(new PropertyTargetWrite(target, "settings", InteropValue.From(candidate)));
        }

        if (!Properties.ApplyTargetChanges($"Edit {Target.TypeName} particles", writes)) lastError = "Writing the particle settings was rejected.";
        else lastError = string.Empty;
    }

    //把 working 里被改动的叶字段搬进目标配置，未改动的字段保留目标自己的值。
    //列表长度变化按整段替换：新增或删除条目本身就是一次整体编辑。
    private static void ApplyEditedFields(object source, object edited, object destination)
    {
        foreach (FieldInfo field in source.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public))
        {
            object? before = field.GetValue(source);
            object? after = field.GetValue(edited);
            if (field.FieldType.IsArray)
            {
                Array original = (Array)before!;
                Array changed = (Array)after!;
                Array current = (Array)field.GetValue(destination)!;
                if (original.Length != changed.Length)
                {
                    field.SetValue(destination, changed);
                    continue;
                }

                for (int index = 0; index < original.Length; ++index)
                {
                    object element = current.GetValue(index)!;
                    ApplyEditedFields(original.GetValue(index)!, changed.GetValue(index)!, element);
                    current.SetValue(element, index);
                }
                continue;
            }

            //嵌套的配置结构继续下钻；枚举与数值类型（含 color、vector3）本身就是叶
            if (field.FieldType.IsValueType && !field.FieldType.IsEnum &&
                field.FieldType.FullName!.StartsWith("Orbeden.Particle", StringComparison.Ordinal))
            {
                object nested = field.GetValue(destination)!;
                ApplyEditedFields(before!, after!, nested);
                field.SetValue(destination, nested);
                continue;
            }

            if (!Equals(before, after)) field.SetValue(destination, after);
        }
    }

    private void DrawStatus()
    {
        bool enabled = false;
        PropertyValue? property = FindProperty("enabled");
        if (property != null)
        {
            enabled = property.Value.Value is bool value && value;
            bool edited = enabled;
            if (EditorGUI.Checkbox("Enabled", ref edited)) SetValue("enabled", InteropValue.From(edited));
        }

        ParticlePreviewInfoAbi info = default;
        bool hasInfo = EditorApplication.TryGetParticlePreviewInfo(Target.ObjectId, ref info);
        if (hasInfo)
        {
            EditorGUI.Label($"Preview {DescribeState(info.State)}  Time {info.Time:F2}s  Particles {info.AliveCount}  Trails {info.TrailCount}  Emitted {info.EmittedCount}  Rejected {info.RejectedCount}");
        }

        //诊断文本来自组件本身，不在 ABI 里返回临时字符串
        Orbeden.ParticleSystem? component = Target.Ens.GetComponent<Orbeden.ParticleSystem>();
        string error = component?.GetLastError() ?? string.Empty;
        if (error.Length != 0) EditorGUI.TextColored($"Error: {error}", new color { r = 1.0f, g = 0.5f, b = 0.35f, a = 1.0f });
    }

    private static string DescribeState(uint state)
    {
        return state switch
        {
            0 => "Stopped",
            1 => "Playing",
            2 => "Paused",
            3 => "Draining",
            _ => "Unknown",
        };
    }

    //编辑态预览：四个按钮各发起一次命令
    private void DrawPreviewControls()
    {
        if (EditorGUI.Button("Play")) EditorApplication.ControlParticlePreview(Target.ObjectId, 0);
        EditorGUI.SameLine();
        if (EditorGUI.Button("Pause")) EditorApplication.ControlParticlePreview(Target.ObjectId, 1);
        EditorGUI.SameLine();
        if (EditorGUI.Button("Reset")) EditorApplication.ControlParticlePreview(Target.ObjectId, 2);
        EditorGUI.SameLine();
        if (EditorGUI.Button("Stop")) EditorApplication.ControlParticlePreview(Target.ObjectId, 3);
    }

    private static bool FloatField(string label, ref float value, float minimum, float maximum)
    {
        bool changed = EditorGUI.InputFloat(label, ref value);
        if (!float.IsFinite(value)) return false;
        float clamped = Math.Clamp(value, minimum, maximum);
        if (MathF.Abs(clamped - value) < 1.0e-9f) return changed;
        value = clamped;
        return true;
    }

    private static bool IntField(string label, ref int value, int minimum, int maximum)
    {
        bool changed = EditorGUI.InputInt(label, ref value);
        int clamped = Math.Clamp(value, minimum, maximum);
        if (clamped == value) return changed;
        value = clamped;
        return true;
    }

    private static bool RangeField(string label, ref ParticleFloatRange range, float minimum, float maximum)
    {
        bool changed = false;
        float low = range.min;
        float high = range.max;
        EditorGUI.Label(label);
        changed |= FloatField($"Min##{label}", ref low, minimum, maximum);
        changed |= FloatField($"Max##{label}", ref high, minimum, maximum);
        if (high < low) (low, high) = (high, low);
        range.min = low;
        range.max = high;
        return changed;
    }

    /// <summary>通过共享颜色选择器编辑粒子初始颜色。</summary>
    private static bool ColorField(string label, ref color value)
    {
        return GUI.ColorField(label, ref value);
    }

    private bool DrawMain()
    {
        //折叠也要走 finally：原生在 Begin 里已经压了 ID、样式与子窗，漏掉收尾会留下孤儿子窗
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Main", "particle-main");
        try
        {
            if (!expanded) return false;
            ParticleMainSettings main = working.main;
            bool changed = false;
            int capacity = (int)main.maxParticles;
            if (IntField("Max Particles", ref capacity, 1, 65536)) { main.maxParticles = (uint)capacity; changed = true; }
            changed |= FloatField("Duration", ref main.duration, 0.01f, 3600.0f);
            changed |= EditorGUI.Checkbox("Looping", ref main.looping);
            changed |= EditorGUI.Checkbox("Play On Awake", ref main.playOnAwake);
            changed |= FloatField("Start Delay", ref main.startDelay, 0.0f, 3600.0f);

            changed |= EnumCombo("Simulation Space", ref main.simulationSpace);

            int seed = (int)main.randomSeed;
            if (IntField("Random Seed", ref seed, 1, int.MaxValue)) { main.randomSeed = (uint)seed; changed = true; }

            changed |= RangeField("Start Lifetime", ref main.startLifetime, 0.001f, 3600.0f);
            changed |= RangeField("Start Speed", ref main.startSpeed, 0.0f, 100000.0f);
            changed |= RangeField("Start Size", ref main.startSize, 0.0001f, 100000.0f);
            changed |= RangeField("Start Rotation", ref main.startRotation, -360000.0f, 360000.0f);
            changed |= ColorField("Start Color", ref main.startColor);

            working.main = main;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawEmission()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Emission", "particle-emission");
        try
        {
            if (!expanded) return false;
            ParticleEmissionSettings emission = working.emission;
            bool changed = EditorGUI.Checkbox("Enabled", ref emission.enabled);
            changed |= FloatField("Rate Over Time", ref emission.rateOverTime, 0.0f, 100000.0f);

            EditorGUI.Label($"Bursts ({emission.bursts.Length})");
            int removeIndex = -1;
            for (int index = 0; index < emission.bursts.Length; ++index)
            {
                ParticleBurst burst = emission.bursts[index];
                bool rowChanged = false;
                float time = burst.time;
                rowChanged |= FloatField($"Time {index}", ref time, 0.0f, MathF.Max(0.01f, working.main.duration - 1.0e-4f));
                burst.time = time;

                int count = (int)burst.count;
                if (IntField($"Count {index}", ref count, 1, 65536)) { burst.count = (uint)count; rowChanged = true; }
                int cycles = (int)burst.cycles;
                if (IntField($"Cycles {index}", ref cycles, 1, 1024)) { burst.cycles = (uint)cycles; rowChanged = true; }
                rowChanged |= FloatField($"Interval {index}", ref burst.interval, 0.000001f, 3600.0f);
                rowChanged |= FloatField($"Probability {index}", ref burst.probability, 0.0f, 1.0f);
                if (EditorGUI.Button($"Delete Burst {index}")) removeIndex = index;
                else if (rowChanged) emission.bursts[index] = burst;
                changed |= rowChanged;
                EditorGUI.Separator();
            }

            if (removeIndex >= 0)
            {
                List<ParticleBurst> bursts = [.. emission.bursts];
                bursts.RemoveAt(removeIndex);
                emission.bursts = [.. bursts];
                changed = true;
            }

            if (emission.bursts.Length < 64 && EditorGUI.Button("Add Burst"))
            {
                emission.bursts = [.. emission.bursts, new ParticleBurst { time = 0.0f, count = 10, cycles = 1, interval = 0.1f, probability = 1.0f }];
                changed = true;
            }

            working.emission = emission;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawShape()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Shape", "particle-shape");
        try
        {
            if (!expanded) return false;
            ParticleShapeSettings shape = working.shape;
            bool changed = EnumCombo("Shape", ref shape.shape);
            switch (shape.shape)
            {
                case ParticleShape.Sphere:
                    changed |= FloatField("Radius", ref shape.radius, 0.0f, 100000.0f);
                    changed |= EditorGUI.Checkbox("Surface Only", ref shape.surfaceOnly);
                    break;
                case ParticleShape.Cone:
                    changed |= FloatField("Radius", ref shape.radius, 0.0f, 100000.0f);
                    changed |= FloatField("Cone Angle", ref shape.coneAngle, 0.0f, 89.0f);
                    changed |= EditorGUI.Checkbox("Surface Only", ref shape.surfaceOnly);
                    break;
                case ParticleShape.Box:
                    changed |= EditorGUI.InputVector3("Box Extents", ref shape.boxExtents);
                    shape.boxExtents.x = Math.Clamp(shape.boxExtents.x, 0.0f, 100000.0f);
                    shape.boxExtents.y = Math.Clamp(shape.boxExtents.y, 0.0f, 100000.0f);
                    shape.boxExtents.z = Math.Clamp(shape.boxExtents.z, 0.0f, 100000.0f);
                    changed |= EditorGUI.Checkbox("Surface Only", ref shape.surfaceOnly);
                    break;
            }

            working.shape = shape;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawMotion()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Motion", "particle-motion");
        try
        {
            if (!expanded) return false;
            ParticleMotionSettings motion = working.motion;
            bool changed = FloatField("Gravity Multiplier", ref motion.gravityMultiplier, -100.0f, 100.0f);
            changed |= EditorGUI.InputVector3("Acceleration", ref motion.acceleration);
            motion.acceleration.x = Math.Clamp(motion.acceleration.x, -100000.0f, 100000.0f);
            motion.acceleration.y = Math.Clamp(motion.acceleration.y, -100000.0f, 100000.0f);
            motion.acceleration.z = Math.Clamp(motion.acceleration.z, -100000.0f, 100000.0f);
            changed |= FloatField("Drag", ref motion.drag, 0.0f, 1000.0f);

            EditorGUI.Label("Size multiplier over normalized lifetime");
            changed |= GUI.AnimationCurve("Size Over Lifetime", $"{Target.ObjectId}:size", ref motion.sizeOverLifetime, 0.0f);

            EditorGUI.Label("Angular velocity (degrees / second)");
            changed |= GUI.AnimationCurve("Angular Velocity Over Lifetime", $"{Target.ObjectId}:angular", ref motion.angularVelocityOverLifetime, float.MinValue);

            changed |= GUI.ColorGradient("Color Over Lifetime", $"{Target.ObjectId}:color", ref motion.colorOverLifetime);

            working.motion = motion;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawCollision()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Collision", "particle-collision");
        try
        {
            if (!expanded) return false;
            ParticleCollisionSettings collision = working.collision;
            bool changed = EditorGUI.Checkbox("Enabled", ref collision.enabled);
            int layerMask = unchecked((int)collision.layerMask);
            if (EditorGUI.InputInt("Layer Mask", ref layerMask)) { collision.layerMask = unchecked((uint)layerMask); changed = true; }
            changed |= FloatField("Radius Scale", ref collision.radiusScale, 0.0001f, 1000.0f);
            changed |= FloatField("Restitution", ref collision.restitution, 0.0f, 1.0f);
            changed |= FloatField("Friction", ref collision.friction, 0.0f, 1.0f);
            changed |= FloatField("Lifetime Loss", ref collision.lifetimeLoss, 0.0f, 1.0f);
            changed |= EnumCombo("Response", ref collision.response);

            working.collision = collision;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawTrails()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Trails", "particle-trails");
        try
        {
            if (!expanded) return false;
            ParticleTrailSettings trails = working.trails;
            bool changed = EditorGUI.Checkbox("Enabled", ref trails.enabled);
            changed |= FloatField("Lifetime", ref trails.lifetime, 0.001f, 60.0f);
            changed |= FloatField("Minimum Vertex Distance", ref trails.minimumVertexDistance, 0.0f, 100000.0f);
            changed |= FloatField("Maximum Vertex Interval", ref trails.maximumVertexInterval, 1.0f / 240.0f, 1.0f);
            int points = (int)trails.maxPointsPerTrail;
            if (IntField("Max Points Per Trail", ref points, 2, 64)) { trails.maxPointsPerTrail = (uint)points; changed = true; }
            int count = (int)trails.maxTrails;
            if (IntField("Max Trails", ref count, 1, 65536)) { trails.maxTrails = (uint)count; changed = true; }
            changed |= FloatField("Width", ref trails.width, 0.0f, 100000.0f);
            changed |= FloatField("Texture Tile Length", ref trails.textureTileLength, 0.0001f, 100000.0f);
            changed |= EditorGUI.Checkbox("Die With Particle", ref trails.dieWithParticle);

            EditorGUI.Label("Width multiplier over normalized length");
            changed |= GUI.AnimationCurve("Width Over Length", $"{Target.ObjectId}:trail-width", ref trails.widthOverLength, 0.0f);

            changed |= GUI.ColorGradient("Color Over Length", $"{Target.ObjectId}:trail-color", ref trails.colorOverLength);

            DrawProperty("trailMaterial");

            working.trails = trails;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawRendering()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Rendering", "particle-rendering");
        try
        {
            if (!expanded) return false;
            ParticleRenderSettings rendering = working.rendering;
            bool changed = EnumCombo("Path", ref rendering.path);
            changed |= EnumCombo("Mode", ref rendering.mode);
            changed |= EnumCombo("Blend Mode", ref rendering.blendMode);

            if (rendering.mode == ParticleRenderMode.Mesh)
            {
                DrawProperty("mesh");
                DrawProperty("materials");
            }
            else
            {
                DrawProperty("materials");
            }

            DrawProperty("drawLayer");
            DrawProperty("receiveShadows");
            if (rendering.mode == ParticleRenderMode.Mesh) DrawProperty("castShadows");
            else EditorGUI.TextWrapped("Billboard and trail particles do not cast shadows.");

            int tilesX = (int)rendering.tilesX;
            if (IntField("Tiles X", ref tilesX, 1, 256)) { rendering.tilesX = (uint)tilesX; changed = true; }
            int tilesY = (int)rendering.tilesY;
            if (IntField("Tiles Y", ref tilesY, 1, 256)) { rendering.tilesY = (uint)tilesY; changed = true; }
            changed |= FloatField("Animation Cycles", ref rendering.animationCycles, 0.0f, 1000.0f);
            changed |= EditorGUI.Checkbox("Random Start Frame", ref rendering.randomStartFrame);

            working.rendering = rendering;
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    private bool DrawSubEmitters()
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Sub Emitters", "particle-sub");
        try
        {
            if (!expanded) return false;
            bool changed = false;
            List<ParticleSubEmitterRule> rules = [.. working.subEmitters];
            List<int> removeIndices = [];
            for (int index = 0; index < rules.Count; ++index)
            {
                ParticleSubEmitterRule rule = rules[index];
                bool rowChanged = false;
                EditorGUI.Label($"Rule {index}  Slot {rule.targetSlot}");
                rowChanged |= EnumCombo($"Event {index}", ref rule.@event);
                int count = (int)rule.count;
                if (IntField($"Count {index}", ref count, 1, 65536)) { rule.count = (uint)count; rowChanged = true; }
                rowChanged |= FloatField($"Probability {index}", ref rule.probability, 0.0f, 1.0f);
                rowChanged |= EditorGUI.Checkbox($"Inherit Velocity {index}", ref rule.inheritVelocity);
                rowChanged |= EditorGUI.Checkbox($"Inherit Color {index}", ref rule.inheritColor);
                rowChanged |= EditorGUI.Checkbox($"Inherit Size {index}", ref rule.inheritSize);

                //目标与规则同行显示，隐藏底层槽号
                DrawProperty($"subEmitterTargets[{rule.targetSlot}]");
                if (EditorGUI.Button($"Delete Rule {index}")) removeIndices.Add(index);
                else if (rowChanged) rules[index] = rule;
                changed |= rowChanged;
                EditorGUI.Separator();
            }

            if (removeIndices.Count != 0)
            {
                foreach (int index in removeIndices.OrderByDescending(value => value)) rules.RemoveAt(index);
                changed = true;
            }

            if (rules.Count < 16 && EditorGUI.Button("Add Rule"))
            {
                rules.Add(new ParticleSubEmitterRule
                {
                    targetSlot = (uint)FindFreeSlot(rules),
                    @event = ParticleSubEmitterEvent.Birth,
                    count = 1,
                    probability = 1.0f,
                });
                changed = true;
            }

            working.subEmitters = [.. rules];
            return changed;
        }
        finally { EditorGUI.EndComponentBlock(); }
    }

    //占用最小未使用的槽位，删除规则时不移动其它槽
    private static int FindFreeSlot(List<ParticleSubEmitterRule> rules)
    {
        for (int slot = 0; slot < 16; ++slot)
        {
            if (!rules.Any(rule => rule.targetSlot == slot)) return slot;
        }

        return 0;
    }

    //枚举下拉；选项顺序与原生枚举序号一致
    private static bool EnumCombo<T>(string label, ref T value) where T : struct, Enum
    {
        bool changed = false;
        if (!EditorGUI.BeginCombo(label, value.ToString())) return false;
        foreach (T option in Enum.GetValues<T>())
        {
            if (!EditorGUI.Selectable(option.ToString(), option.Equals(value))) continue;
            value = option;
            changed = true;
        }
        EditorGUI.EndCombo();
        return changed;
    }
}
