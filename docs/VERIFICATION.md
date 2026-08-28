# D-SF — Registro Canônico de Verificação

## 1. Finalidade

Este documento contém a evidência reproduzível ativa do baseline R0–R2.1:

- condições de teste;
- hashes canônicos;
- workloads;
- timings observados;
- comparações entre implementações;
- sanitizers/compiladores;
- limitações que impedem conclusões mais fortes.

Ele não transforma benchmark de laboratório em promessa de produção.

---

## 2. Regra de interpretação

Todo número neste documento deve ser lido como:

> “foi observado neste workload e ambiente registrados”.

Nunca como:

> “a engine completa sempre executará nessa velocidade”.

Particularmente:

- `position += velocity * dt` não equivale a NPC completo;
- throughput de scheduler compute-only não equivale a gameplay autoritativo;
- CPU reference result não equivale a GPU result;
- x86-64 Linux não prova Windows/ARM;
- duas versões de compilador não provam determinismo universal.

---

## 3. Ambiente registrado

Os artefatos finais R2/R2.1 registraram:

- Linux x86-64;
- 5 CPUs/cores disponíveis no ambiente compartilhado;
- host model exposto: AMD EPYC 9V74;
- GCC 14.2;
- Clang 17;
- CMake 3.31.6;
- nenhuma GPU de produção/Vulkan usada nas medições.

Uma etapa anterior da mesma conversa descreveu a sandbox como:

- Intel Xeon Platinum 8573C;
- 5 cores disponíveis;
- aproximadamente 6 GB RAM.

Como a sandbox é virtualizada/compartilhada e a identidade de host exposta diferiu entre execuções, **o modelo de CPU não é tratado como propriedade estável do laboratório**. Os resultados devem ser associados ao workload e à execução, não a uma afirmação de hardware fixo.

---

# R0 — Minimal Authoritative World

## 4. Invariantes testados

1. Entity ID `0` reservado para mutações de world scope.
2. Entity IDs estáveis, sequenciais e sem skip silencioso por `CreateEntity`.
3. Transaction IDs monotônicos.
4. Toda transação validada antes da primeira mutação autoritativa.
5. Valores vetoriais não finitos rejeitados.
6. `AdvanceReference` é uma mutação de world scope e exige `dt` finito e não negativo.
7. Renderer e física não fazem parte do World Kernel de referência.

## 5. Correção arquitetural descoberta durante R1

O LAB-0 possuía `reserve_entity_id()`, que avançava o cursor de identidade fora das transações. Portanto um journal contendo somente transações poderia não reconstruir o cursor futuro exato.

Correção:

- `CreateEntity` passou a alocar o próximo ID dentro da própria transação;
- `next_entity_id()` passou a ser read-only.

Essa correção tornou a identidade transaction-complete no modelo testado.

## 6. Baselines iniciais registrados na conversa

Antes da refatoração R1, um primeiro benchmark de entidades leves foi reportado com aproximadamente:

| Entidades | Tempo CPU reportado |
|---:|---:|
| 100.000 | ~0.140 ms/frame |
| 1.000.000 | ~1.357 ms/frame |
| 3.000.000 | ~3.917 ms/frame |

A atualização intermediária da mesma etapa também citou ~1.22 ms para 1.000.000 de entidades. A diferença é preservada como variação/execução distinta de sandbox; nenhum desses valores é tratado como especificação.

Após o avanço ser transacional, o baseline registrado passou a ser:

### 1.000.000 entidades / 120 frames

- spawn transaction: 3.000.000 mutations;
- spawn commit: ~39.873 ms;
- simulation: ~1.229 ms/frame;
- ~813.5 milhões de updates mínimos de posição/s.

Workload de update:

```text
position += velocity * dt
```

Nenhuma IA, física complexa, animação, pathfinding ou gameplay completo está incluído.

---

# R1 — Change Journal / SHA-256 / Replay / Rollback

## 7. Canonical SHA-256

O hash atual cobre serialização little-endian canônica de:

- schema version;
- next entity ID;
- last transaction ID;
- living entity count;
- cada Entity ID alocado;
- alive flag;
- health;
- raw IEEE-754 bits de position;
- raw IEEE-754 bits de velocity.

O SHA-256 implementado em C++ foi comparado a `Python hashlib.sha256()` para um World conhecido.

Hash esperado e produzido:

```text
9052d221ad22d52fb0a43dbec4410a9546d7bbb968642614ee0deb55758e7c33
```

## 8. Replay determinístico R1

Workload:

- 256 entidades iniciais;
- 5.000 simulation frames;
- 5.001 total transactions incluindo spawn;
- health changes periódicas;
- destruction de entidade;
- criação posterior de nova entidade;
- `AdvanceReference(1/60)` transacional.

Hash final:

```text
9e6b6a3bac5a0564e2f3100bcf7eed9d0e48ef44615382f27931d5dcb9960c57
```

Foi reproduzido por:

- World original;
- replay a partir de World pristine usando somente forward transactions;
- journal após save/load binário;
- GCC 14.2 Release x86-64 Linux;
- Clang 17 Release x86-64 Linux.

## 9. Rollback R1

O histórico de 5.001 transações foi revertido até checkpoint anterior milhares de transações atrás.

Resultado:

- hash restaurado = hash originalmente registrado no checkpoint;
- replay do tail removido voltou ao hash final original;
- rollback de spawn journal-owned restaurou hash do World pristine e o cursor de identidade correspondente.

## 10. Guard de divergência

Se o World não corresponde ao hash tail esperado pelo journal, rollback é recusado.

Objetivo: impedir que undo antigo seja aplicado sobre estado alterado fora da história controlada.

## 11. R1 reference performance

Cenário: 256 entidades, 5.000 frames, full post-commit SHA-256 e exact undo capture.

- journal commit total: ~272.054 ms;
- journal commit average: ~0.054 ms/frame;
- persistent journal file: 224.236 bytes;
- save: ~0.812 ms;
- load: ~0.720 ms;
- replay: ~1.504 ms;
- rollback all: ~275.413 ms.

### Limitação detectada

O modelo R1 favorece correção, não escala:

- `AdvanceReference` undo captura estado anterior de todas as entidades tocadas;
- full World SHA-256 é recalculado por commit journal-owned.

Hash incremental e rollback storage escalável permanecem abertos.

---

# R2 — Dependency Execution Graph

## 12. Correctness tests

A suíte R2 verifica:

1. read/read na mesma wave;
2. read/write serializado;
3. write/write serializado;
4. cycle rejection;
5. undeclared access rejeitado antes do commit;
6. serial e worker pool com mesmo transaction count relevante;
7. serial e paralelo com mesmo final World hash;
8. 120 frames / 4.096 entidades reproduzindo hash exato.

Hash do teste R2 4.096/120:

```text
057e4f9d9e4921cb93da1a0c5b1245fafbadb49ce8755f572ebff34b558e53ca
```

Reproduzido em:

- GCC 14.2;
- Clang 17;
- ThreadSanitizer build do teste concorrente.

## 13. Cross-worker correctness

Workload:

- 8.192 entidades;
- 60 frames;
- 4 sistemas;
- 2 waves.

Workers testados:

- 1;
- 2;
- 4;
- 5.

Todos produziram:

```text
71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c
```

GCC e Clang reproduziram o mesmo hash.

## 14. Sanitizers R2

- AddressSanitizer: suíte corrente passou sem erro reportado;
- UndefinedBehaviorSanitizer: suíte corrente passou sem erro reportado;
- ThreadSanitizer: concurrent R2 test passou sem data race reportada.

Isso aumenta confiança, mas não constitui prova matemática de race freedom para código futuro.

## 15. Scheduler compute-only microbenchmark

Workload:

- 32 sistemas independentes;
- 750.000 deterministic integer iterations/system;
- nenhum authoritative World write;
- 7 samples por worker count;
- mediana reportada.

| Workers | Mediana | Speedup |
|---:|---:|---:|
| 1 | 80.508 ms | 1.000× |
| 2 | 41.275 ms | 1.951× |
| 4 | 24.551 ms | 3.279× |
| 5 | 23.186 ms | 3.472× |

Auxiliary checksum em todos:

```text
3462961269496396242
```

Conclusão limitada:

> O scheduler expõe paralelismo relevante quando o workload é suficientemente independente e compute-heavy.

## 16. Authoritative World R2 benchmark

Workload:

- 8.192 entities;
- 60 frames;
- 4 systems;
- 2 waves;
- per-entity transactional patches;
- 7 samples/worker count;
- mediana.

| Workers | Mediana | Speedup |
|---:|---:|---:|
| 1 | 49.192 ms | 1.000× |
| 2 | 41.956 ms | 1.172× |
| 4 | 46.826 ms | 1.051× |
| 5 | 45.700 ms | 1.076× |

Durante a exploração anterior à mediana final, uma execução chegou a mostrar o worker path aproximadamente 18% mais lento que serial e outra amostragem mostrou ~1.21× para 2 workers e ~1.15× para 4–5. A mediana canônica do relatório é a tabela acima.

### Gargalo identificado

Per-entity writes materializavam grandes quantidades de:

```text
SetPosition(entity)
SetVelocity(entity)
SetHealth(entity)
```

Custo acumulado:

- vector growth;
- allocation/materialization;
- merge;
- validation;
- serial authoritative commit;
- memory traversal.

Decisão: R2 correctness `VERIFIED`; R2 performance `PARTIAL`; criar R2.1 antes de R3.

---

# R2.1 — Hybrid Transaction Patches

## 17. Candidatos comparados

O experimento comparou conceitualmente/na implementação disponível:

1. per-entity oracle;
2. contiguous component ranges;
3. fixed 256-entity pages;
4. page clone / COW-style forward patches;
5. disjoint parallel page publication;
6. persistent parallel publisher para evitar lifecycle artificial.

O objetivo não era eleger uma page size a priori.

## 18. Patch representation equivalence

Workload:

- 4.096 entities;
- 120 frames.

Todos os caminhos testados reproduziram:

```text
a073236582885e8cd53f22aa4825ed539a00c74c7c026e61d9e1db9940ada47e
```

O mesmo conjunto verificou:

- replay do PatchJournal desde base independente;
- rollback até frame 60;
- replay do tail;
- save/load binário;
- overlapping ranges rejeitados sem alteração autoritativa;
- non-finite Vec3 payload rejeitado sem alteração autoritativa.

## 19. Hybrid scalar + range transaction

Uma transação dedicada combinou:

- scalar Health write;
- scalar DestroyEntity;
- dense Position range.

Resultado:

- commit;
- serialize;
- load;
- replay;
- rollback;

com hash exato.

Um scalar Position write sobrepondo Position range foi rejeitado atomicamente.

## 20. Execution Kernel equivalence R2.1

Workload:

- 4.096 entities;
- 120 frames;
- original scalar serial oracle vs range-emitting R2.1 systems;
- worker pool + PatchJournal.

Hash de ambos:

```text
657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94
```

Journal R2.1:

- 240 wave transactions;
- replay = mesmo final hash.

O legacy `execute()` foi testado para rejeitar range-producing systems em vez de ignorá-los silenciosamente.

## 21. Cross-compiler e sanitizers R2.1

Hashes R2.1 exatos reproduzidos em:

- GCC 14.2 x86-64 Linux;
- Clang 17 x86-64 Linux.

Sanitizers:

- ASan/UBSan: dedicated patch + execution tests sem erro reportado;
- TSan: Execution Kernel R2.1 sem race reportada;
- smaller persistent parallel-publisher workload sob TSan sem race reportada;
- full patch TSan não é reivindicado: reference path com repeated thread spawning excedeu o limite de uma invocação da sandbox.

## 22. Integrated R2 vs R2.1 — 8.192 entities / 60 frames

Todos reproduziram:

```text
71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c
```

| Candidate | Median | Speedup vs R2 scalar serial |
|---|---:|---:|
| R2 scalar serial | 44.803 ms | 1.000× |
| R2 scalar, 4 workers | 61.923 ms | 0.724× |
| R2.1 ranges serial | 32.612 ms | 1.374× |
| R2.1 ranges, 4 workers | 35.361 ms | 1.267× |
| R2.1 ranges, 4 workers + persistent parallel commit | 36.034 ms | 1.243× |

Conclusão:

- range representation já ajuda;
- worker overhead ainda pode superar benefício nessa escala.

## 23. Integrated R2 vs R2.1 — 100.000 entities / 20 frames

Todos reproduziram:

```text
e6803f6411816d3e2261f091e7eb82718262ee9969b33dce9135467c9072c2c4
```

| Candidate | Median | Speedup vs R2 scalar serial |
|---|---:|---:|
| R2 scalar serial | 192.925 ms | 1.000× |
| R2 scalar, 4 workers | 151.304 ms | 1.275× |
| R2.1 ranges serial | 138.596 ms | 1.392× |
| R2.1 ranges, 4 workers | 102.150 ms | 1.889× |
| R2.1 ranges, 4 workers + persistent parallel commit | 102.841 ms | 1.876× |

Conclusão:

- dependency-derived workers começam a combinar melhor com menor patch granularity.

## 24. Integrated R2 vs R2.1 — 1.000.000 entities / 3 frames

Todos reproduziram:

```text
61d624a0af70729626dafebd3b3bea4cb5a074e625ec7f17ac981f6eef5a2c60
```

| Candidate | Median | Speedup vs R2 scalar serial |
|---|---:|---:|
| R2 scalar serial | 740.970 ms | 1.000× |
| R2 scalar, 4 workers | 517.654 ms | 1.431× |
| R2.1 ranges serial | 228.456 ms | 3.243× |
| R2.1 ranges, 4 workers | 166.262 ms | 4.457× |
| R2.1 ranges, 4 workers + persistent parallel commit | 150.455 ms | 4.925× |

Interpretação permitida:

> O overhead do modelo scalar R2 cresceu o bastante para dominar este workload denso; typed ranges removeram grande parte desse overhead sem alterar o estado final.

Interpretação proibida:

> “D-SF é 4.925× mais rápido que engines tradicionais.”

Nenhuma engine tradicional foi comparada nesse experimento.

## 25. Dense representation payload

Para 1.000.000 entities, 3 components, 1 frame:

- per-entity oracle: 3.000.000 logical records / ~96.000.000 bytes de mutation-vector capacity;
- contiguous ranges: 3 logical records / ~28.000.120 bytes;
- fixed pages 256: 11.721 logical records / ~28.468.840 bytes.

Ranges removem repetição de entity/kind/header mantendo os valores efetivos de componentes.

## 26. Sparse falsification — 100.000 entities / 200 frames / 1% Position writes

### Clustered 1% — runs de 100 entities

| Candidate | Total reference-run time | Records/frame | Payload/frame |
|---|---:|---:|---:|
| Per-entity scalar | 16.087 ms | 1.000 | 32.512 B |
| Exact ranges | 17.213 ms | 10 | 12.400 B |
| 256-page clone | 18.931 ms | 10 | 31.120 B |
| Full component range | 249.793 ms | 1 | 1.200.040 B |

Ranges reduziram payload, mas scalar permaneceu marginalmente mais rápido neste workload esparso pequeno.

### Scattered 1% — isolated entities

| Candidate | Total reference-run time | Records/frame | Payload/frame |
|---|---:|---:|---:|
| Per-entity scalar | 12.036 ms | 1.000 | 32.512 B |
| Exact one-value ranges | 17.018 ms | 1.000 | 52.000 B |
| 256-page clone | 285.824 ms | 391 | 1.215.640 B |
| Full component range | 249.986 ms | 1 | 1.200.040 B |

Hash equality foi preservada entre candidatos dentro de cada pattern.

### Falsificação

Fixed page 256 não deve substituir scalar globalmente.

Um único changed entity pode forçar movimentação de uma página majoritariamente limpa.

Decisão atual:

```text
structural events       → scalar
sparse scattered writes → scalar
clustered writes        → exact ranges quando vantajoso
dense writes            → large contiguous ranges
fixed pages             → implementation option, not semantic truth
COW pages               → not promoted
parallel publication    → optional optimization when amortized
```

---

## 27. Clean release verification R2.1

Source release version registrada no artefato: `0.0.2`.

### GCC 14.2

- Release;
- x86-64 Linux;
- configured strict warning set;
- 4/4 CTest tests passed;
- no compiler warnings no conjunto configurado.

### Clang 17

- Release;
- x86-64 Linux;
- 4/4 CTest tests passed;
- no compiler warnings no conjunto configurado.

Ambos reproduziram:

```text
657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94
```

O ZIP R2.1 foi extraído em diretório vazio, configurado com CMake, compilado e testado novamente; 100% dos testes passaram.

SHA-256 do ZIP R2.1 entregue na conversa:

```text
a4d0bcdef114e84456758c6c0067df50b1aaf599bda7686945d433aa9137cd63
```

---

## 28. Limitações abertas que impedem `FOUNDATIONAL`

1. Windows x86-64 replay/cross-machine.
2. ARM replay.
3. política explícita de floating-point determinism.
4. CPU ↔ GPU authoritative equivalence.
5. incremental/Merkle state hash.
6. bounded/compressed rollback history.
7. crash-safe journal checksums/recovery.
8. automatic scalar-vs-range coalescing.
9. sparse masked-page representation intermediária.
10. NUMA/cache affinity.
11. GPU-resident patch production/publication.
12. Spatial Kernel ainda não selecionado.
13. Geometry Kernel ainda não construído.

Nenhuma dessas limitações invalida o resultado de referência correspondente; elas delimitam seu alcance.
