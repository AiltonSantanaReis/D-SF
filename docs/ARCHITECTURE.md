# D-SF — Arquitetura Ativa

Status: arquitetura experimental consolidada até **R5E-HW04**. Nenhum contrato está `FOUNDATIONAL`.

## Invariantes ativos

1. `World` é autoritativo; renderer, physics, spatial, geometry e device são views/compilações derivadas.
2. `VisualGeometry != PhysicalGeometry`.
3. workers/sistemas não recebem autoridade irrestrita; mudanças autoritativas passam por contratos validados.
4. otimizações semanticamente equivalentes devem possuir reference/oracle quando aplicável.
5. backend API não deve contaminar os contratos upstream.

## Camadas

```text
Authoritative World
  ├─ State / Identity
  ├─ Hybrid Transactions
  └─ Change Journal
        |
        +--> Execution Kernel
        |      └─ dependency waves / private patches / atomic commit
        |
        +--> Shared Spatial Snapshot
        |      ├─ Wide BVH8 view
        |      └─ Morton BVH8 view
        |
        +--> GeometrySet
        |      ├─ Clustered Triangle
        |      └─ Sparse / Analytic SDF
        |
        +--> Device Geometry Packages
               └─ Residency Manager
                      |
                      +--> DeviceWorkPacket DAG
                              |
                              +--> Backend Translation
                                      |
                                      +--> Vulkan hardware gates
```

## Transactions / execution

R2.1 mantém duas lanes sob a mesma autoridade: scalar sparse writes e typed contiguous ranges. R2 Execution Kernel declara resources `reads/writes/after`, constrói waves determinísticas e faz merge canônico em uma transação por wave. WRITE não implica READ.

## Spatial

`Shared Spatial Snapshot` é um data plane derivado do World e reduz duplicação de bounds. Views espaciais podem ser reconstruídas/refitadas sem redefinir identidade do objeto. Política ativa é custo-aware; manutenção pesada pode ser cooperativa/budgeted. Não existe uma única estrutura espacial universal promovida.

## Geometry

`GeometryHandle {ProviderId, Generation, ResourceId}` identifica representação. `GeometrySet` permite múltiplas representações do mesmo source revision, com capabilities e error certificate. Seleção usa hard constraints + objetivo explícito + Pareto frontier; não existe score mágico universal.

Runtime representations verificadas incluem Sparse Implicit Geometry e Clustered Triangle Surface. Telemetria e safe exploration são explícitas e workload/device/batch scoped.

## Device separation

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

`DeviceResourceKey` possui owner namespace/object/revision, resource class e subresource. Geometry packages nascem de `RepresentationArchive` canônico. Residency é explícita e atomic group upload deve publicar tudo ou nada.

`DeviceWorkPacket` é coarse execution node, com domain, program key, launch mode, resources/access, parameters e explicit dependencies. Fine work deve morar em work-items/indirect/device-generated data, não em dezenas de milhares de host packets.

## Backend translation

R5D diferencia semantic identity de backend lowering. Direct static pode permanecer Direct ou ser promovido quando policy/capability permitem. Indirect dinâmico não é silenciosamente demovido para Direct. DeviceGenerated não é silenciosamente demovido para host-driven modes.

Launch control é uma dependência de resource explícita e readable para Indirect/DeviceGenerated. Descriptor model separa unique descriptor table de per-command resource uses. Barrier lowering representa hazards cross-wave; translator rejeita hazard inválido dentro da mesma wave em vez de repará-lo silenciosamente.

## Vulkan hardware state

R5E-HW01 a HW04 verificaram na RTX 3070 Ti:

- real Vulkan capability discovery;
- real `VkDevice` / memory upload-readback;
- Direct compute;
- Indirect compute por `vkCmdDispatchIndirect` com resident control buffer;
- exact CPU oracle equality para 1,048,576 `uint32` no workload HW03/HW04;
- zero validation errors/warnings nesses gates.

Isso prova correção funcional no escopo testado. Não prova superioridade de performance, DGC runtime, descriptor heap runtime ou policy final de synchronization.

## Próxima fronteira

HW05 deve adicionar timestamps GPU e caracterização Direct/Indirect sem mudar simultaneamente workload, shader e binding. Depois disso, DGC/descriptor candidates poderão ser testados com baselines reais.
