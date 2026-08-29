# D-SF — Research Ledger

## 0. Função e regras deste ledger

Este é o registro cronológico canônico da pesquisa D-SF até o baseline R2.1 e de todas as decisões que levaram a ele.

### Origem permitida para este baseline

O conteúdo abaixo usa exclusivamente:

1. mensagens desta conversa;
2. código e relatórios de sandbox produzidos nesta conversa;
3. resultados de compilação/testes/benchmarks executados durante esta conversa.

Nenhum histórico de outro repositório foi usado para reconstruir o passado do D-SF.

### O que este ledger preserva

- intenção inicial;
- tecnologias consideradas;
- o que foi considerado real, exagerado ou não comprovado;
- arquitetura proposta antes de existir código;
- limitações reais da sandbox;
- cada fase implementada R0, R1, R2 e R2.1;
- falhas arquiteturais encontradas;
- benchmarks intermediários e finais;
- hashes de verificação;
- razões de mudança de roadmap;
- hipóteses rejeitadas ou limitadas;
- limitações que permanecem abertas;
- mandato de centralização no repositório D-SF.

O ledger não é apagado quando a arquitetura muda. Novas entradas devem **superseder**, não reescrever silenciosamente, decisões antigas.

---

# PARTE I — ORIGEM DA PESQUISA

## L-001 — Análise inicial das tecnologias não convencionais

### Entrada de origem

O primeiro texto apresentado para análise propunha substituir ou superar pipelines tradicionais baseados em malhas/rasterização por cinco famílias:

1. motores de renderização neural — NeRF e 3D Gaussian Splatting;
2. Neural World Simulators;
3. Signed Distance Fields + raymarching;
4. Sparse Voxel Octrees;
5. arquiteturas compute-first com ECS em VRAM.

As vantagens alegadas incluíam:

- eliminar modelagem/retopologia/UV;
- inferir física, colisão e iluminação em uma única inferência neural;
- “detalhamento infinito” e booleanas “perfeitas” em SDF;
- eliminar LOD manual em SVO;
- executar ECS, física e interações totalmente em VRAM;
- manipular centenas de milhões de objetos com overhead muito baixo.

### Avaliação registrada na conversa

A conclusão não foi “as tecnologias são falsas”. A conclusão foi:

> As tecnologias são reais, porém o texto mistura pesquisa legítima com extrapolações muito mais fortes do que os resultados sustentam.

#### NeRF / 3D Gaussian Splatting

Conclusões registradas:

- NeRF é representação neural implícita real.
- 3D Gaussian Splatting não deve ser descrito como “geometria latente” genérica.
- 3DGS trabalha com primitivas gaussianas explícitas contendo posição/forma/orientação/opacidade/cor ou parâmetros equivalentes.
- differentiable rasterization é importante sobretudo para otimização/training; “renderizar via diferenciação” é descrição imprecisa.
- manipular gaussianas por transforms espaciais não equivale a “manipular objetos em espaço latente”.
- 3DGS pode reduzir muito etapas de captura/reconstrução para certos ambientes.
- 3DGS não fornece automaticamente superfície física, topologia, collider, UV, interior/exterior, rigging, destruição ou material PBR tradicional.

Decisão conceitual inicial:

> 3DGS parece mais forte como novo tipo de asset/renderer dentro de uma engine híbrida do que como substituto universal da geometria de gameplay.

#### Neural World Simulators

Conclusões registradas:

- modelos que predizem frames futuros a partir de ações são reais e pesquisa relevante;
- um modelo pode gerar dinâmica visual plausível sem possuir explicitamente `mass`, `collider`, `health`, `door_locked` etc.;
- aparência de física plausível não equivale a estado físico autoritativo;
- jogos competitivos, replay, networking e lógica de estado exigem controle/determinismo que um gerador probabilístico de frames não garante por padrão.

Decisão conceitual inicial:

> Neural world models devem ser considerados sistemas opcionais/propositivos, não autoridade do mundo, até que seja demonstrado o contrário.

#### SDF + Raymarching

Conclusões registradas:

- SDF é tecnologia real e forte para CSG, edição geométrica e destruição;
- `f(x,y,z)=d` fornece uma forma natural de representar distância à superfície;
- `union`, `intersection`, `subtract` e blends podem ser expressos diretamente;
- “detalhamento infinito” foi rejeitado como afirmação literal porque precisão numérica, resolução, VRAM, bandwidth e número de passos continuam finitos;
- “booleanas perfeitas” foi reduzido para “booleanas muito mais naturais”, pois materiais, detalhe fino, sampling e normal estimation continuam problemáticos;
- destruição por subtração de volumes foi considerada uma direção de pesquisa especialmente promissora.

Decisão conceitual inicial:

> SDF foi classificado como uma das melhores candidatas para uma engine experimental, principalmente para geometria procedural, terreno e destruição.

#### Sparse Voxel Octree

Conclusões registradas:

- SVO é estrutura real e útil para volumes esparsos/hierárquicos;
- LOD pode emergir naturalmente da hierarquia, mas streaming, nível a carregar/renderizar e transições continuam sendo decisões de sistema;
- editar density/voxel não cria automaticamente um solver de física de deformação;
- hardware RT não “entende SVO automaticamente”; aceleração ainda exige representação/intersection path adequado.

Decisão conceitual inicial:

> Sparse volumes permanecem candidatos fortes de infraestrutura, especialmente combinados com SDF, mas sem promessa de eliminar todos os problemas de LOD/física.

#### ECS / Compute-first em VRAM

Conclusões registradas:

- GPU-driven culling, LOD, compaction e indirect drawing são reais;
- grandes workloads uniformes podem se beneficiar muito da GPU;
- “eliminar a CPU” foi rejeitado;
- OS, input, filesystem, network, editor, audio, scripting e workloads branch-heavy/irregulares continuam fortes candidatos a CPU;
- “centenas de milhões de objetos” só pode ser defendido quando “objeto” é uma unidade extremamente simples; não como ator completo com IA/física/animação.

Decisão conceitual inicial:

> O alvo correto é minimizar sincronização e movimentação desnecessária CPU↔GPU, não transformar “CPU zero” em dogma.

### D-001 — Primeira direção arquitetural

**Problema:** nenhuma das cinco tecnologias vence universalmente todos os workloads.

**Alternativas:** escolher uma representação única vs engine híbrida.

**Decisão:** investigar engine híbrida com múltiplas representações nativas.

**Razão:** meshes, SDF, voxels, splats e futuros métodos possuem perfis de vantagem diferentes.

**Estado:** `HYPOTHESIS`, não comprovado até R2.1.

---

## L-002 — Reformulação do objetivo: não “matar polígonos”, mas desacoplar o mundo da representação

A conversa avançou para a pergunta: se fosse designado um programa de pesquisa capaz de tentar redesenhar a indústria, qual seria o ponto de partida?

A resposta registrada alterou o foco:

> Não começar pelo renderer. Começar definindo o que é o mundo e como sua verdade sobrevive à substituição de representações.

A arquitetura conceitual proposta foi:

```text
WORLD STATE
  ├── Semantic State
  └── Spatial State
          ↓
      WORLD FIELD / world description
          ↓
representation/compiler layer
  ├── Mesh
  ├── SDF
  ├── Voxel
  ├── Gaussian
  └── future representation
          ↓
derived views
  ├── Render
  ├── Physics
  └── Audio
```

### D-002 — Geometria visual não é autoridade

**Decisão conceitual:** o objeto não deve ser definido pelo mesh.

Exemplo discutido:

```text
Object 73942
Semantic:
    material = rock
    density = ...
    hardness = ...
Spatial:
    region = ...
Representations:
    MeshRepresentation
    SDFRepresentation
    SparseVolumeRepresentation
```

Um mesmo objeto poderia futuramente usar:

- mesh perto;
- SDF para destruição;
- SDF coarse para colisão;
- splat distante;
- voxel/volume para GI.

**Estado até R2.1:** apenas direção arquitetural; Geometry Kernel ainda não existe.

---

# PARTE II — CONSTITUIÇÃO CONCEITUAL ANTES DO CÓDIGO

## L-003 — Proposta dos cinco núcleos iniciais

Antes do LAB-0, a conversa propôs um `World Kernel` mínimo com cinco responsabilidades pesquisáveis.

### State Kernel

Responsabilidade proposta:

- verdade lógica do mundo;
- estado como `health`, `locked`, `closed` etc.;
- renderer não pode alterar essa verdade diretamente.

### Space Kernel

Hipótese proposta:

- evitar scene graph como autoridade espacial;
- experimentar `Hierarchical Sparse Space` ou estruturas similares;
- separar organização espacial de hierarquia lógica.

Essa hipótese foi deliberadamente adiada até R3.

### Memory Kernel

Hipótese proposta:

```text
Virtual World Memory
resource can be:
CPU resident
GPU resident
duplicated
compressed
streamed
generated
discarded
```

Interface imaginada:

```text
require(resource, GPU, deadline = frame + 1)
```

Nenhum Memory Kernel foi implementado até R2.1.

### Execution Kernel

Hipótese proposta:

- sistema declara `reads`, `writes`, constraints;
- scheduler decide ordem e paralelismo;
- CPU/GPU placement deve ser implementação onde possível.

Essa hipótese tornou-se R2 e foi parcialmente comprovada para CPU worker pool.

### Change Kernel

Hipótese proposta:

- alterações autoritativas como transações;
- possível infraestrutura comum para undo/redo, replay, replication, save, rollback e debug temporal.

R0/R1 comprovaram parte da hipótese: transação, journal, replay e rollback no escopo de referência.

Networking/editor undo ainda não foram comprovados.

---

## L-004 — Geometry Kernel conceitual

Foi proposto um contrato futuro:

```text
GeometryProvider
├── TriangleProvider
├── SDFProvider
├── VoxelProvider
├── GaussianProvider
├── CurveProvider
├── ParticleProvider
└── NeuralProvider
```

Foi discutido que as capacidades de cada provider seriam diferentes, por exemplo:

- Mesh: render/animação fortes;
- SDF: CSG/destruição fortes;
- Voxel: matéria/volume fortes;
- Gaussian: visual/captura forte, física difícil;
- Neural: experimental.

Nenhum `GeometryProvider` foi implementado até R2.1.

---

## L-005 — Renderer heterogêneo conceitual

A conversa propôs um renderer que recebesse uma `RenderView`, não entities diretamente.

Caminho conceitual:

```text
World
→ Visibility
→ Representation Selection
→ Render Compilation
→ GPU command graph
```

Um frame poderia futuramente combinar:

- rasterization;
- ray tracing;
- raymarching;
- Gaussian splatting;
- volume rendering;
- neural reconstruction.

Foi proposta a ideia de convergir diferentes origens para um `Surface Buffer` comum:

```text
position
normal
material
depth
velocity
semantic ID
```

Isso permaneceu hipótese.

---

## L-006 — Física como derived view

Foi registrada a separação:

```text
VisualGeometry != PhysicalGeometry
```

Exemplos conceituais:

- Gaussian visual + SDF physical proxy;
- detailed mesh visual + convex hull physical;
- destructible voxel visual + coarse volume physical.

Nenhum Physics View production foi implementado até R2.1.

---

## L-007 — Política de IA

Regra proposta:

```text
AI proposes.
Kernel validates.
```

Exemplos:

- modelo propõe destino; navigation/physics valida;
- neural model propõe geometria; Geometry Kernel converte/valida;
- modelo não se torna autoridade apenas porque gera aparência plausível.

Essa regra permanece princípio de pesquisa, não implementação até R2.1.

---

## L-008 — Método do laboratório

Foi definido que cada pesquisa teria:

```text
Theory
Experiment
Data
Conclusion
Specification
```

Resultados possíveis:

```text
PASS
PARTIAL
FAIL
```

Uma ideia deveria poder morrer mesmo se fosse originalmente defendida.

Exemplo explícito discutido:

```text
GPU ECS = 5.9 ms
CPU ECS = 2.1 ms
→ GPU ECS perdeu naquele workload
```

Nenhum benchmark seria manipulado para salvar uma preferência arquitetural.

### D-003 — Oracle/reference implementation

Para cada kernel importante, manter um caminho simples e correto, mesmo que lento.

Objetivo:

```text
optimized implementation
vs
reference implementation
```

Essa decisão foi implementada em R2/R2.1 como serial/per-entity oracle contra caminhos paralelos/range.

---

## L-009 — Estados de maturidade

Estados inicialmente discutidos e depois formalizados:

```text
IDEA
→ HYPOTHESIS
→ EXPERIMENTAL
→ VERIFIED
→ FOUNDATIONAL
```

Regra central:

> `FOUNDATIONAL` estabiliza contrato, não congela implementação.

Até R2.1: nenhum contrato foi promovido a `FOUNDATIONAL`.

---

## L-010 — Testes adversariais conceituais

Cenários sugeridos para fases futuras:

- 1 bilhão de entidades vazias;
- 10 milhões de objetos móveis;
- explosão destruindo 1 km³;
- teleporte instantâneo;
- alteração de 1 milhão de materiais;
- GPU com pouca VRAM;
- CPU com poucos threads;
- alta latência CPU↔GPU;
- streaming loss;
- destruição durante ray tracing;
- mundo procedural amplo/infinito.

Esses itens não foram executados até R2.1; são exemplos de metodologia adversarial.

---

## L-011 — Deterministic vs non-deterministic domains

Foi proposto separar:

### Deterministic domain

- gameplay;
- multiplayer;
- física autoritativa relevante;
- replay;
- transactions.

### Non-deterministic domain

- particles;
- visual AI;
- GI;
- denoising;
- procedural decoration.

Nenhuma política completa foi implementada; R1/R2 apenas demonstraram exact-state determinism nos workloads registrados.

---

## L-012 — Error Budget e Computational Economy

Duas ideias futuras foram registradas.

### Error Budget

Em vez de somente `LOD0/1/2`, declarar algo como:

```text
max_screen_error = 0.25 pixels
```

A engine escolheria uma representação adequada ao orçamento/erro.

### Computational Economy

Frame teria orçamento dinâmico entre systems, potencialmente redistribuído quando física/render/etc. consumissem menos ou mais.

Objetivo conceitual:

```text
quality of world / computational cost
```

Nenhuma das duas ideias foi implementada até R2.1.

---

## L-013 — Ordem de construção escolhida

Foi rejeitado começar por editor.

Ordem conceitual:

```text
Engine Lab
→ Runtime
→ Developer Tools
→ Editor
```

O primeiro protótipo deveria ser um laboratório de stress/estado, não um jogo completo.

### D-004 — Primeiro gate real

Primeira propriedade a provar:

> Um `World State` pode existir sem conhecer renderer, mesh, collider, scene graph ou GPU?

Isso levou diretamente ao LAB-0/R0.

---

# PARTE III — CAPACIDADE DA SANDBOX E LAB-0

## L-014 — Limitações da sandbox

Quando foi perguntado se todo o projeto poderia ser desenvolvido na sandbox, a verificação registrou:

Disponível:

- GCC 14.2;
- Clang 17;
- CMake 3.31;
- Python 3.13;
- C/C++/multithreading/SIMD/estrutura de dados/testes/benchmarks CPU.

Não disponível para evidência real:

- GPU NVIDIA exposta;
- Vulkan device de produção para benchmark;
- VRAM/timestamps/occupancy reais.

### D-005 — Política de GPU evidence

**Decisão:** código GPU pode ser preparado na sandbox, mas nenhum número será chamado de medição de GPU sem execução em hardware real.

Essa regra foi incorporada à Constituição.

---

## L-015 — LAB-0 criado

O primeiro laboratório C++23 foi criado com:

- `World Kernel` de referência;
- `EntityId`;
- `World State`;
- `Position`;
- `Velocity`;
- `Health`;
- Transaction System;
- SoA storage;
- reference simulation;
- tests;
- benchmarks;
- architecture constitution.

Build reportado:

- GCC 14.2;
- C++23;
- CMake 3.31;
- Linux x86-64.

Testes iniciais:

```text
100% tests passed
0 tests failed
```

Foi testado que uma transação contendo operação válida seguida de inválida não deveria produzir mudança parcial.

---

## L-016 — LAB-0 performance baseline

Resultados reportados em uma execução:

| Entidades | Tempo por frame CPU |
|---:|---:|
| 100.000 | ~0.140 ms |
| 1.000.000 | ~1.357 ms |
| 3.000.000 | ~3.917 ms |

Throughput aproximado reportado:

- ~737 milhões entity-updates/s para 1 milhão;
- ~766 milhões entity-updates/s para 3 milhões.

Uma atualização intermediária citou ~1.22 ms para 1 milhão.

Interpretação registrada:

- workload = integração mínima de posição/velocidade;
- não é benchmark de personagens/NPCs completos;
- objetivo = baseline contra o qual SIMD, multithreading e GPU futuros possam ser comparados.

---

# PARTE IV — R0/R1

## L-017 — Diretiva para concluir R0 e entrar em R1

Foi explicitamente escolhido:

```text
R0 completion
→ R1 Change Journal
→ State Hash
→ deterministic replay
→ rollback
```

Critério de sucesso proposto:

> executar milhares de frames, persistir transações, reconstruir World pristine e obter exatamente o mesmo hash final.

---

## L-018 — Falha encontrada: `reserve_entity_id()`

Ao revisar LAB-0 para R1, foi encontrado que:

```text
reserve_entity_id()
```

alterava o cursor de identidade fora das transações.

Consequência:

```text
initial world + persisted transaction journal
```

poderia não reconstruir integralmente o futuro cursor de IDs.

### D-006 — Identity allocation transaction-derived

Correção:

- `CreateEntity` passa a determinar/alocar o ID dentro da transação;
- `next_entity_id()` é read-only.

Razão:

> Toda informação necessária para reconstrução autoritativa precisa estar derivável do estado inicial + história autoritativa.

---

## L-019 — `AdvanceReference` virou mutação autoritativa

Antes, `world.integrate_reference(dt)` podia modificar diretamente posições.

Isso quebraria a regra de mutação transacional.

Correção:

```text
Transaction N
└── AdvanceReference(dt)
```

O frame persistente passa a poder conter somente o comando determinístico de avanço, sem registrar cada posição resultante.

---

## L-020 — Forward journal vs ephemeral undo

Foi separada a história em duas responsabilidades:

### Persistente

- forward transactions somente.

### Temporário

- pre-transaction state necessário para exact undo/rollback.

### D-007 — Separar formato persistente de undo

Razão:

> Um journal compacto e reproduzível não precisa assumir que o formato mais conveniente para persistência é o mesmo formato de rollback em memória.

---

## L-021 — R1 replay proof

Workload:

- 256 initial entities;
- 5.000 frames;
- 5.001 transactions;
- health mutations;
- destruction;
- later create;
- `AdvanceReference`.

Hash final:

```text
9e6b6a3bac5a0564e2f3100bcf7eed9d0e48ef44615382f27931d5dcb9960c57
```

Procedimento:

1. executar World original;
2. persistir journal;
3. criar novo `World()`;
4. aplicar apenas forward transactions em ordem;
5. comparar SHA-256.

Resultado:

- mesmo hash final.

---

## L-022 — Cross-compiler R1

O mesmo workload produziu o mesmo hash com:

- GCC 14.2;
- Clang 17;
- x86-64 Linux.

Conclusão permitida:

> deterministic replay foi observado entre os compiladores testados na mesma arquitetura/ambiente de referência.

Conclusão proibida:

> determinismo universal Windows/Linux/ARM/GPU.

---

## L-023 — SHA-256 cross-check

A implementação C++ do hash foi comparada a Python `hashlib.sha256()` para um estado conhecido.

Hash:

```text
9052d221ad22d52fb0a43dbec4410a9546d7bbb968642614ee0deb55758e7c33
```

Ambas as implementações produziram exatamente esse valor.

---

## L-024 — R1 rollback proof

O journal foi revertido de final para checkpoint anterior.

Validações:

- hash do rollback = hash original daquele checkpoint;
- reapply tail = hash final original;
- full rollback do spawn journal-owned = pristine world hash + identity cursor restaurado.

---

## L-025 — Rollback divergence guard

Foi adicionado guard de hash.

Se algum código modifica World fora do journal e o hash atual não corresponde ao tail esperado:

```text
ROLLBACK REJECTED
```

### D-008 — Não aplicar undo sobre estado divergente

Razão:

> Stale undo em World divergente é pior do que recusar rollback, porque pode produzir corrupção silenciosa.

---

## L-026 — R1 performance

Cenário 256 entities / 5.000 frames:

- journal commit total: ~272.054 ms;
- ~0.054 ms/frame;
- journal: 224.236 bytes;
- save: ~0.812 ms;
- load: ~0.720 ms;
- replay: ~1.504 ms;
- rollback all: ~275.413 ms.

O próprio experimento revelou custos não escaláveis:

- undo de `AdvanceReference` guarda old states das entidades tocadas;
- full World SHA-256 por commit é O(World size).

### D-009 — Não congelar implementação R1

Contrato funcionou; implementação não foi promovida como final.

Pesquisas futuras listadas:

- page/chunk hashing;
- Merkle-style hierarchy;
- copy-on-write history;
- bounded rollback;
- compressed undo.

---

## L-027 — R0 baseline após integração transacional

1.000.000 entities / 120 frames:

- spawn: 3.000.000 mutations;
- spawn commit ~39.873 ms;
- simulation ~1.229 ms/frame;
- ~813.5M minimal position updates/s.

Resultado reforçou que a transação compacta de `AdvanceReference` não exigia persistir um milhão de posições por frame.

---

## L-028 — Status após R1

Classificação registrada:

```text
R0 VERIFIED — reference scope
R1 VERIFIED — same-architecture reference scope
FOUNDATIONAL NONE
```

Próxima hipótese: R2 Execution Kernel.

---

# PARTE V — R2 EXECUTION KERNEL

## L-029 — Hipótese R2

Hipótese explicitada:

> O sistema pode declarar dependências de dados e permitir que o kernel determine ordem/paralelismo seguro sem hard-coded frame phases, mantendo um commit autoritativo determinístico.

Regra de aceitação:

```text
Serial World Hash == Parallel World Hash
```

Caso contrário, o executor paralelo falha mesmo sendo mais rápido.

---

## L-030 — Private patches, não direct World writes

Precaução adicionada antes do benchmark:

Sistemas na mesma wave não podem escrever `World` diretamente.

Caminho:

```text
immutable pre-wave World
→ System A/B/C private computation
→ Patch A/B/C
→ canonical merge
→ Transaction
→ World
```

### D-010 — Workers compute proposals; transaction owns authority

Razão:

> Preservar R0/R1 e impedir que scheduling paralelo torne a verdade do World dependente de timing de thread.

---

## L-031 — Resource access model

Recursos iniciais:

- Identity;
- EntityState;
- Position;
- Velocity;
- Health.

Cada system declara:

- stable `SystemId`;
- read set;
- write set;
- optional `after` dependencies;
- system function.

`SystemContext` fiscaliza o contrato no path de referência.

---

## L-032 — Hazard rules

Implementadas/testadas:

```text
READ X  + READ X  → parallel-safe
READ X  + WRITE X → serialize
WRITE X + READ X  → serialize
WRITE X + WRITE X → serialize
```

Write permission não implica read permission.

Undeclared access falha antes de qualquer commit da wave.

---

## L-033 — DAG e cycles

Explicit dependencies entram no grafo.

Cycles são rejeitados.

Para hazards ambíguos, stable `SystemId` foi usado como tie-break determinístico no reference model.

Isso não foi promovido como frame-phase universal.

---

## L-034 — Deterministic waves

Kahn topological sorting produz waves.

Cada wave:

1. observa mesmo pre-wave World;
2. executa systems serial ou concurrent;
3. coleta private outputs;
4. ordena por stable SystemId;
5. funde em uma transaction;
6. commit antes da próxima wave.

---

## L-035 — Primeira correção de lifecycle do R2

O primeiro prototype criava worker pool por `execute()`.

Benchmark mostrou que thread creation/lifecycle contaminava frame time.

### D-011 — Persistent `ExecutionRuntime`

Correção:

- worker threads persistem entre frames;
- `ExecutionKernel::execute()` permanece convenience one-shot path;
- benchmark de scheduler usa runtime persistente.

Razão:

> Não comparar scheduler paralelo carregando artificialmente custo de criação de threads a cada frame.

---

## L-036 — R2 correctness proof

Testes passaram para:

- read/read same wave;
- read/write serialization;
- write/write serialization;
- cycle rejection;
- undeclared access rejection;
- serial/parallel convergence.

4.096 entities / 120 frames hash:

```text
057e4f9d9e4921cb93da1a0c5b1245fafbadb49ce8755f572ebff34b558e53ca
```

Reproduzido GCC/Clang/TSan test path.

---

## L-037 — Cross-worker R2

8.192 entities / 60 frames / 4 systems / 2 waves.

Workers:

```text
1, 2, 4, 5
```

Todos produziram:

```text
71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c
```

---

## L-038 — R2 compute-only benchmark

32 independent systems / 750.000 deterministic integer iterations each.

Medianas:

| Workers | Median | Speedup |
|---:|---:|---:|
| 1 | 80.508 ms | 1.000× |
| 2 | 41.275 ms | 1.951× |
| 4 | 24.551 ms | 3.279× |
| 5 | 23.186 ms | 3.472× |

Checksum:

```text
3462961269496396242
```

Conclusão:

> O worker scheduler consegue expor paralelismo forte quando tasks são independentes e compute-heavy.

---

## L-039 — R2 authoritative benchmark

8.192 entities / 60 frames / 4 systems / 2 waves / per-entity patches.

Medianas finais canônicas:

| Workers | Median | Speedup |
|---:|---:|---:|
| 1 | 49.192 ms | 1.000× |
| 2 | 41.956 ms | 1.172× |
| 4 | 46.826 ms | 1.051× |
| 5 | 45.700 ms | 1.076× |

Durante exploração, uma medição mostrou 4 workers cerca de 18% pior que serial e outra amostragem indicou ~1.21× em 2 workers e ~1.15× em 4–5. O relatório final usou medianas acima.

Conclusão:

> Scheduler paralelo funciona; world workload não escala proporcionalmente.

---

## L-040 — Gargalo R2 identificado

O problema observado:

```text
Worker computes
→ SetPosition(entity 1)
→ SetPosition(entity 2)
→ ...
→ thousands/millions Mutation records
→ merge
→ validate
→ serial commit
```

Custos:

- mutation objects;
- vector growth;
- metadata repetition;
- memory bandwidth;
- merge;
- validation;
- serial publication.

### D-012 — R2 performance = PARTIAL

Não mascarar como sucesso completo.

R2 foi fechado como correctness experiment, performance parcial.

---

## L-041 — Sanitizers R2

Registrado:

- ASan pass;
- UBSan pass;
- TSan concurrent test sem reported data race.

Limite declarado:

> Sanitizer pass aumenta evidência, não prova matemática de ausência universal de races.

---

## L-042 — Mudança de roadmap: inserir R2.1 antes de R3

Roadmap original seguiria:

```text
R2 → R3 Spatial Kernel
```

Os dados mostraram que isso carregaria um authority-boundary bottleneck não resolvido para o Spatial Kernel.

### D-013 — Inserir R2.1

Novo roadmap:

```text
R0
R1
R2
R2.1 Hybrid/Chunked Transaction Patches
R3 Spatial Kernel
```

Razão:

> Resolver o gargalo que o laboratório efetivamente encontrou antes de construir outra camada sobre ele.

---

# PARTE VI — R2.1 HYBRID TRANSACTION PATCHES

## L-043 — Hipótese R2.1

Pergunta:

> Podemos manter exact R0/R1/R2 semantics sem materializar uma `Mutation` para cada entity/component alterado?

Candidatos planejados:

1. per-entity mutations — oracle;
2. contiguous range patches;
3. fixed-size page/chunk patches;
4. copy-on-write pages;
5. deterministic/disjoint parallel commit.

Regra:

```text
Hash(A) == Hash(B) == Hash(C) == Hash(D) == Hash(E)
```

quando semanticamente equivalentes.

---

## L-044 — Primeira decisão R2.1: estrutura e dense writes não precisam da mesma granularidade

Antes do benchmark foi observado:

- `Create/Destroy` são naturalmente discrete structural events;
- Position/Velocity/Health densos podem ser ranges.

### D-014 — Uma authority boundary, múltiplas granularidades

Não criar dois sistemas de estado.

Criar uma transaction híbrida:

```text
PatchTransaction
├── scalar_mutations
├── vec3 ranges
└── u32 ranges
```

Tudo sob um único `TransactionId`.

---

## L-045 — Patch equivalence proof

4.096 entities / 120 frames.

Comparados:

- per-entity oracle;
- contiguous ranges;
- fixed pages 256;
- page clone/COW-style forward patch;
- disjoint parallel publication;
- persistent parallel publisher.

Todos produziram:

```text
a073236582885e8cd53f22aa4825ed539a00c74c7c026e61d9e1db9940ada47e
```

Também passaram:

- PatchJournal replay;
- rollback frame 60;
- tail replay;
- binary save/load;
- overlap rejection;
- non-finite Vec3 rejection.

---

## L-046 — Primeiro benchmark R2.1 e duas hipóteses falsificadas cedo

Resultados exploratórios mostraram:

1. pages de 256 não eram automaticamente melhores;
2. criar threads dentro de cada commit tornava `parallel commit` injustamente caro.

Para 1 milhão de entities, o range contíguo reduziu payload temporário aproximado:

```text
~96 MB → ~28 MB
```

no workload dense de três componentes.

Uma primeira execução mostrou range ~1.25× mais rápido que scalar oracle antes do benchmark integrado final.

### D-015 — Persistent parallel publisher

Assim como R2 worker lifecycle, parallel publication deveria usar workers persistentes para não incluir thread creation em cada commit.

---

## L-047 — Sparse workload decide contra “one page size fits all”

100.000 entities / 200 frames / 1% Position writes.

### Clustered 1%

- scalar: 1.000 records/frame, ~32.512 B payload/frame, 16.087 ms total reference run;
- exact ranges: 10 records/frame, ~12.400 B, 17.213 ms;
- 256-page clone: 10 records/frame, ~31.120 B, 18.931 ms;
- full range: 1 record/frame, ~1.200.040 B, 249.793 ms.

Conclusão:

- range reduziu payload;
- scalar ainda foi marginalmente mais rápido neste sparse small workload.

### Scattered 1%

- scalar: 1.000 records/frame, ~32.512 B, 12.036 ms;
- one-value ranges: 1.000 records, ~52.000 B, 17.018 ms;
- 256-page clone: 391 pages, ~1.215.640 B, 285.824 ms;
- full component range: ~1.200.040 B, 249.986 ms.

Conclusão:

> fixed page 256 pode mover ~100× mais Position data do que o necessário em pattern esparso espalhado.

### D-016 — Rejeitar fixed page universal

Decisão:

```text
structural         → scalar
sparse scattered   → scalar
clustered          → exact ranges quando benéfico
dense              → large ranges
fixed pages        → optional implementation detail
COW                 → not promoted
parallel publish    → optional when amortized
```

Essa foi uma falsificação explícita de uma possível arquitetura universal de pages.

---

## L-048 — No silent precedence

Foi testado conflito:

```text
scalar SetPosition(entity 5)
+
PositionRange[4..8]
```

Resultado:

```text
REJECT
```

### D-017 — Overlap ambíguo é erro

Não criar regras implícitas “range ganha” ou “scalar ganha”.

Razão:

> Precedência escondida cria bugs difíceis de auditar e torna state result dependente de detalhe de representação.

---

## L-049 — Hybrid scalar + range transaction proof

Uma mesma transaction combinou:

- scalar Health write;
- scalar DestroyEntity;
- dense Position range.

Ela passou:

- commit;
- serialize;
- load;
- replay;
- rollback.

Portanto lanes diferentes continuam uma única fronteira autoritativa.

---

## L-050 — R2 Execution integration

`SystemContext` ganhou:

```text
set_position_range
set_velocity_range
set_health_range
```

O legacy `execute()` rejeita range-producing system.

Novo `execute_patched()`:

1. usa mesmo DAG R2;
2. same immutable pre-wave World;
3. coleta scalar/range private outputs;
4. stable SystemId sort;
5. merge em um hybrid PatchTransaction/wave;
6. full validate;
7. publish.

### D-018 — Scheduling semantics separados de patch storage

Razão:

> Otimizar granularidade de transaction não deve exigir redesenhar a lógica de dependências.

---

## L-051 — R2.1 execution equivalence proof

4.096 entities / 120 frames.

Original scalar serial oracle vs range-emitting worker-pool + PatchJournal:

```text
657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94
```

Mesmo hash.

Journal:

- 240 wave transactions;
- replay reproduziu exact final hash.

---

## L-052 — Integrated benchmark: 8.192 entities

60 frames.

Hash:

```text
71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c
```

| Candidate | Median | Speedup |
|---|---:|---:|
| R2 scalar serial | 44.803 ms | 1.000× |
| R2 scalar / 4 workers | 61.923 ms | 0.724× |
| R2.1 ranges serial | 32.612 ms | 1.374× |
| R2.1 ranges / 4 workers | 35.361 ms | 1.267× |
| R2.1 ranges / 4 workers + persistent parallel commit | 36.034 ms | 1.243× |

Conclusão:

- ranges ajudam;
- worker overhead pode piorar small/medium workload.

---

## L-053 — Integrated benchmark: 100.000 entities

20 frames.

Hash:

```text
e6803f6411816d3e2261f091e7eb82718262ee9969b33dce9135467c9072c2c4
```

| Candidate | Median | Speedup |
|---|---:|---:|
| R2 scalar serial | 192.925 ms | 1.000× |
| R2 scalar / 4 workers | 151.304 ms | 1.275× |
| R2.1 ranges serial | 138.596 ms | 1.392× |
| R2.1 ranges / 4 workers | 102.150 ms | 1.889× |
| R2.1 ranges / 4 workers + persistent parallel commit | 102.841 ms | 1.876× |

Conclusão:

- redução de patch overhead permite que worker parallelism apareça com mais clareza.

---

## L-054 — Integrated benchmark: 1.000.000 entities

3 frames / 4 systems / 2 waves.

Hash:

```text
61d624a0af70729626dafebd3b3bea4cb5a074e625ec7f17ac981f6eef5a2c60
```

| Candidate | Median | Speedup |
|---|---:|---:|
| R2 scalar serial | 740.970 ms | 1.000× |
| R2 scalar / 4 workers | 517.654 ms | 1.431× |
| R2.1 ranges serial | 228.456 ms | 3.243× |
| R2.1 ranges / 4 workers | 166.262 ms | 4.457× |
| R2.1 ranges / 4 workers + persistent parallel commit | 150.455 ms | 4.925× |

### Interpretação registrada

Não declarar “engine 4.925× mais rápida”.

Declarar:

> Neste workload autoritativo denso, substituir milhões de scalar Mutation records por typed ranges removeu overhead suficiente para reduzir fortemente o tempo mantendo exact same final SHA-256.

---

## L-055 — Dense payload evidence

1.000.000 entities / 3 components / 1 frame:

```text
per-entity oracle:
3,000,000 records
~96,000,000 bytes vector capacity

contiguous range:
3 records
~28,000,120 bytes

256 pages:
11,721 records
~28,468,840 bytes
```

O ganho de range vem de remover metadata repetida, não de eliminar os component values.

---

## L-056 — Cross-compiler e sanitizers R2.1

Hashes exatos reproduzidos em:

- GCC 14.2 x86-64 Linux;
- Clang 17 x86-64 Linux.

Checks:

- ASan/UBSan dedicated R2.1 patch/execution tests sem erro reportado;
- TSan R2.1 Execution Kernel sem race reportada;
- smaller persistent publisher TSan sem race reportada;
- full patch TSan não reivindicado porque repeated thread-spawning reference path excedeu limite de invocação.

### D-019 — Timeout não conta como aprovação

Quando full instrumented suite excedeu limite, foi executada em partes/dedicated tests.

Razão:

> Timeout não deve ser convertido em “pass” por conveniência.

---

## L-057 — Release hygiene R2.1

Release source foi reconstruído limpo com:

- GCC 14.2;
- Clang 17;
- 4/4 tests em ambos;
- zero warnings no strict warning set configurado.

O ZIP foi extraído em diretório vazio, reconfigurado via CMake, compilado e testado.

Result:

```text
100% tests passed
0 tests failed
```

R2.1 ZIP SHA-256 entregue:

```text
a4d0bcdef114e84456758c6c0067df50b1aaf599bda7686945d433aa9137cd63
```

---

## L-058 — Status após R2.1

```text
R0 VERIFIED — reference scope
R1 VERIFIED — same-architecture reference scope
R2 VERIFIED — correctness; performance partial
R2.1 VERIFIED — reference correctness + tested performance
FOUNDATIONAL NONE
```

### D-020 — R3 liberado

R2.1 resolveu o gargalo estrutural suficiente para justificar iniciar Spatial Kernel research.

Isso não significa que hybrid patches são final/foundational.

---

# PARTE VII — R3 DIREÇÃO DE PESQUISA

## L-059 — Spatial Kernel Bake-Off definido como próxima etapa

Nenhum vencedor foi escolhido antecipadamente.

Candidatos citados:

- Flat/Uniform Grid;
- Hash Grid;
- BVH;
- Sparse Brick Hierarchy;
- Octree;
- possivelmente Hierarchical Hash Grid.

Workloads planejados:

- static dense;
- static sparse;
- moving objects;
- teleports;
- regional updates/destruction;
- streaming;
- near/range queries;
- ray queries;
- large queries;
- million-object scale updates.

Métricas:

- RAM;
- build time;
- update time;
- query time;
- cache behavior quando mensurável;
- thread scalability;
- integração com authority patch boundary.

### D-021 — R3 sem preferência tecnológica

Nenhuma SVO/octree/BVH/grid será promovida por reputação ou sofisticação.

O mesmo contrato e workload decidirão.

---

# PARTE VIII — CENTRALIZAÇÃO NO GITHUB D-SF

## L-060 — Mandato de repositório oficial

O usuário criou:

```text
git@github.com:AiltonSantanaReis/D-SF.git
```

E determinou que:

- o desenvolvimento deste projeto deve ficar neste repositório;
- o acesso de projeto deve se limitar a ele;
- todo plano, ideia, resultado de teste, decisão e razão deve ser registrado;
- o histórico utilizado para reconstruir o baseline deve ser somente esta conversa;
- documentos precisam ser centralizados e atualizados, evitando cópias concorrentes;
- README precisa conter o desenho da arquitetura;
- qualidade documental e padronização são mais importantes do que quantidade de arquivos;
- arquivos de estado não devem receber afirmações como fato sem evidência devidamente comprovada;
- intenção inicial, estágio atual, destino, regras e ponto de fechamento precisam ficar explícitos.

### Verificação do repositório

O GitHub connector confirmou:

```text
repository: AiltonSantanaReis/D-SF
visibility: public
default branch: main
size at inspection: 0
permissions available to authenticated connector:
admin / maintain / pull / push / triage
```

Nenhum outro repositório foi consultado para esta consolidação.

### D-022 — D-SF como única fonte oficial

**Decisão:** a partir deste baseline, o repositório `AiltonSantanaReis/D-SF` é o system of record do projeto.

### D-023 — Consolidação documental

Relatórios antigos separados (`CONSTITUTION`, `EXPERIMENTS`, `R0_R1_REPORT`, `R2_REPORT`, `R2_1_REPORT`, `RELEASE_VERIFICATION`) continham informação correta, porém sobreposta.

Para evitar divergência futura, o conteúdo comprovado foi consolidado em cinco documentos canônicos:

```text
README.md
PROJECT.md
ARCHITECTURE.md
RESEARCH_LEDGER.md
VERIFICATION.md
```

Os antigos relatórios não são importados como fontes ativas concorrentes.

Razão:

> O Git history preserva versões; o projeto precisa de uma única verdade ativa por categoria de informação.

### D-024 — Preservar código R0–R2.1 sem rename cosmético nesta importação

O código verificado usa namespace/binários `aion`.

A consolidação não renomeia o código apenas para alinhar branding, porque isso misturaria mudança cosmética com importação do baseline comprovado.

O README declara explicitamente que `aion` é nome interno herdado e não contrato final.

Razão:

> Baseline de auditoria deve minimizar mudanças não necessárias no código que gerou a evidência existente.

---

# PARTE IX — DECISION REGISTER COMPACTO

Esta tabela é índice; os detalhes e razões estão nas entradas correspondentes acima.

| ID | Decisão | Estado |
|---|---|---|
| D-001 | Pesquisar engine híbrida em vez de uma representação universal. | HYPOTHESIS |
| D-002 | Tratar geometria visual como representação derivada, não identidade do mundo. | HYPOTHESIS / direção |
| D-003 | Manter oracle/reference antes de otimizações. | VERIFIED como método usado em R2/R2.1 |
| D-004 | Começar provando World independente de renderer/mesh/physics. | Executado em R0 |
| D-005 | Não fabricar evidência de GPU na sandbox. | Regra ativa |
| D-006 | Identity allocation dentro de CreateEntity transaction. | VERIFIED R1 |
| D-007 | Forward persisted journal separado de ephemeral undo. | VERIFIED R1 |
| D-008 | Rollback rejeita World divergente do journal. | VERIFIED R1 |
| D-009 | Não congelar full-hash/full-undo implementation de R1. | Regra ativa |
| D-010 | Workers produzem proposals/patches; não mutam World diretamente. | VERIFIED R2 |
| D-011 | Worker pool/runtime persistente. | VERIFIED R2 implementation |
| D-012 | R2 performance marcada PARTIAL por patch bottleneck. | Evidência R2 |
| D-013 | Inserir R2.1 antes de R3. | Concluído |
| D-014 | Hybrid transaction com múltiplas granularidades e uma authority boundary. | VERIFIED R2.1 |
| D-015 | Parallel publisher persistente para benchmark justo. | VERIFIED implementation path |
| D-016 | Fixed page 256 não é solução universal. | FALSIFIED as universal |
| D-017 | No silent scalar/range precedence. | VERIFIED R2.1 |
| D-018 | Scheduling semantics separados de patch storage. | VERIFIED integration R2.1 |
| D-019 | Timeout/instrumentation incompleta não pode ser chamado de pass. | Regra ativa |
| D-020 | R3 pode começar após R2.1. | Ativo |
| D-021 | R3 escolherá estrutura por bake-off, não preferência. | Regra próxima fase |
| D-022 | GitHub D-SF é fonte oficial única do projeto. | Ativo |
| D-023 | Cinco documentos canônicos; sem relatórios ativos duplicados. | Ativo |
| D-024 | Preservar código `aion` testado no baseline; naming futuro separado. | Ativo no baseline |

---

# PARTE X — DÍVIDA / PERGUNTAS ABERTAS NO MOMENTO DA CENTRALIZAÇÃO

Estas perguntas permanecem abertas e **não devem ser relatadas como resolvidas**:

1. Qual SpatialBackend vence cada distribuição/workload?
2. Um único backend espacial é suficiente ou a solução deve ser híbrida?
3. Como declarar disjoint spatial/resource ranges no scheduler sem explodir graph cost?
4. Como substituir full World SHA-256 por incremental/Merkle sem enfraquecer proof?
5. Qual modelo de rollback/history escala melhor: bounded window, COW, compressed delta, page snapshots ou combinação?
6. Como fazer crash-safe journal persistence/recovery?
7. Como definir floating-point determinism cross-platform?
8. Como validar Windows/Linux e x86/ARM?
9. Quando scalar-to-range coalescing deve ser automático?
10. Existe uma representação intermediária masked-page útil entre scalar e dense range?
11. Como tratar NUMA/cache affinity?
12. Como produzir patches na GPU sem perder auditabilidade/authority?
13. Qual será o contrato real de GeometryProvider?
14. Como mesh/SDF/voxel/splat coexistirão sem duplicate truth?
15. Como gerar Physical View diferente de Render View mantendo consistência?
16. Como medir representational error/quality budget?
17. Como integrar GPU real e medir timestamps/bandwidth/residency?
18. Como testar Gaussian/SDF/voxel em produção sem assumir que substituem mesh universalmente?
19. Em que momento um contrato sobreviveu evidência suficiente para `FOUNDATIONAL`?
20. Qual naming público final substituirá ou não o namespace interno `aion`?

---

# PARTE XI — PRÓXIMA ENTRADA ESPERADA

A próxima entrada de pesquisa deve ser R3 e conter, antes de qualquer benchmark de performance:

1. hipótese formal;
2. `SpatialBackend` contract;
3. referência/oracle de query;
4. geração determinística dos datasets;
5. candidatos implementados de maneira comparável;
6. correctness equivalence;
7. adversarial distributions;
8. benchmarks;
9. decisão por workload;
10. razão para promoção, limitação ou rejeição.

Nenhuma estrutura espacial pode entrar na seção `VERIFIED` do `ARCHITECTURE.md` antes dessa evidência.
