# RFC-003: Feature Catalog

**Project:** MarketPulse v2 – Market Intelligence Engine (MPIE)

**Status:** Draft v1.0

**Author:** Muskan Srivastav

---

# 1. Purpose

This document defines every quantitative feature supported by the MarketPulse Intelligence Engine.

Each feature specification includes:

* Mathematical definition
* Market intuition
* Required inputs
* State requirements
* Computational complexity
* Dependency graph
* Execution pass
* Implementation priority

The Feature Catalog acts as the single source of truth for feature implementation.

---

# 2. Design Principles

Every feature must satisfy four requirements.

## Deterministic

Given identical MarketEvents, the feature must always produce identical outputs.

---

## Incremental

Whenever possible, computation should be performed incrementally instead of recomputing historical windows.

---

## Constant Time

The preferred computational complexity is:

O(1)

per MarketEvent.

Rolling statistics should update incrementally using pre-allocated ring buffers.

---

## Explainable

Every feature must answer a meaningful market question.

If the feature cannot be interpreted by a quantitative researcher, it does not belong in the engine.

---

# 3. Feature Classification

Features are grouped into four categories.

```
Price Features

↓

Order Book Features

↓

Order Flow Features

↓

Statistical Features
```

Future versions may introduce:

* Liquidity Features
* Impact Models
* Risk Features
* Alpha Features

---

# 4. Price Features (Pass 1)

---

## 4.1 Spread

**Definition**

Spread = AskPrice − BidPrice

**Interpretation**

Measures the instantaneous transaction cost.

Smaller spreads generally indicate higher liquidity.

**Inputs**

* Best Bid
* Best Ask

**Dependencies**

None

**Stateful**

No

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

## 4.2 Mid Price

**Definition**

MidPrice = (Bid + Ask) / 2

**Interpretation**

Represents the fair market price between buyers and sellers.

**Inputs**

* Best Bid
* Best Ask

**Dependencies**

None

**Stateful**

No

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

## 4.3 Microprice

**Definition**

MicroPrice =

(Bid × AskSize + Ask × BidSize)

/

(BidSize + AskSize)

**Interpretation**

Estimates the expected future transaction price by incorporating queue sizes.

Moves toward the side with greater pressure.

**Inputs**

* Bid Price
* Ask Price
* Bid Size
* Ask Size

**Dependencies**

None

**Stateful**

No

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

# 5. Order Book Features (Pass 2)

---

## 5.1 Queue Imbalance

**Definition**

(BidVolume − AskVolume)

/

(BidVolume + AskVolume)

**Interpretation**

Measures dominance between bid-side and ask-side liquidity.

Positive values indicate stronger buying pressure.

Negative values indicate stronger selling pressure.

**Inputs**

* Bid Size
* Ask Size

**Dependencies**

None

**Stateful**

No

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

## 5.2 Market Depth

**Definition**

Total visible liquidity across tracked order book levels.

**Interpretation**

Measures available executable liquidity.

Higher depth generally implies greater market stability.

**Inputs**

* L2 Book

**Dependencies**

None

**Stateful**

No

**Complexity**

O(levels)

**Priority**

Extended (v2.1)

---

## 5.3 Liquidity Score

**Definition**

Composite metric combining:

* Spread
* Depth
* Queue imbalance

**Interpretation**

Provides a normalized estimate of current market liquidity.

**Dependencies**

Spread

Queue Imbalance

Market Depth

**Stateful**

No

**Complexity**

O(1)

**Priority**

Extended (v2.1)

---

# 6. Order Flow Features (Pass 3)

---

## 6.1 Signed Volume

**Definition**

BuyVolume − SellVolume

over the observation window.

**Interpretation**

Measures directional trading pressure.

Positive values indicate aggressive buying.

**Inputs**

Trades

Trade Direction

**Dependencies**

None

**Stateful**

Yes

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

## 6.2 Order Flow Imbalance (OFI)

**Definition**

Change in bid liquidity

minus

change in ask liquidity

between consecutive events.

**Interpretation**

Measures which side of the order book is becoming stronger.

One of the most widely used market microstructure features.

**Inputs**

Current Book

Previous Book

**Dependencies**

Previous MarketEvent

**Stateful**

Yes

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

## 6.3 Trade Intensity

**Definition**

Number of trades

per observation window.

**Interpretation**

Measures market activity.

Sudden increases often coincide with volatility.

**Inputs**

Trade timestamps

**Stateful**

Yes

**Complexity**

O(1)

**Priority**

Extended (v2.1)

---

# 7. Statistical Features (Pass 4)

---

## 7.1 Returns

**Definition**

Log Return

ln(Current MidPrice / Previous MidPrice)

**Interpretation**

Fundamental building block for statistical models.

**Inputs**

Current MidPrice

Previous MidPrice

**Dependencies**

MidPrice

**Stateful**

Yes

**Complexity**

O(1)

**Priority**

Core (v2.0)

---

## 7.2 VWAP

**Definition**

Σ(Price × Volume)

/

Σ(Volume)

**Interpretation**

Benchmark execution price widely used in institutional trading.

**Inputs**

Trades

Trade Volume

**Stateful**

Yes

**Complexity**

O(1)

**Priority**

Extended (v2.1)

---

## 7.3 Realized Volatility

**Definition**

Rolling standard deviation of log returns.

**Interpretation**

Measures recent market uncertainty.

**Inputs**

Returns

Rolling Window

**Dependencies**

Returns

**Stateful**

Yes

**Complexity**

O(1)

(using incremental updates)

**Priority**

Core (v2.0)

---

# 8. Advanced Features (Future)

These features are intentionally excluded from v2.0 but the architecture supports them.

---

## VPIN

Measures toxicity of order flow.

Priority:

v3

---

## Kyle's Lambda

Measures market impact.

Priority:

v3

---

## Amihud Illiquidity

Measures price response to trading volume.

Priority:

v3

---

## Roll Spread Estimator

Estimates effective spread from transaction prices.

Priority:

v3

---

## Order Book Slope

Measures liquidity distribution across depth.

Priority:

v3

---

# 9. Feature Dependency Graph

```
Spread
MidPrice
MicroPrice
QueueImbalance
SignedVolume
        │
        ▼

Returns
        │
        ▼

Realized Volatility

Market Depth
        │
        ▼

Liquidity Score

Previous Book
        │
        ▼

Order Flow Imbalance
```

The Feature Registry validates this dependency graph during engine startup.

Circular dependencies are prohibited.

---

# 10. Feature Metadata

Every feature exposes a FeatureDescriptor.

Required metadata includes:

* Name
* Category
* Version
* Schema Version
* Computational Complexity
* Stateful / Stateless
* Execution Pass
* Required Inputs
* Dependencies
* Lookback Window
* Units

This metadata enables automatic validation, documentation generation, and future plugin discovery.

---

# 11. Implementation Roadmap

## Phase 1 — Runtime Validation

* Spread
* MidPrice
* MicroPrice

Purpose:

Validate execution pipeline.

---

## Phase 2 — Market Microstructure

* Queue Imbalance
* Signed Volume
* Order Flow Imbalance

Purpose:

Validate state management.

---

## Phase 3 — Statistical Layer

* Returns
* Realized Volatility

Purpose:

Validate rolling windows.

---

## Phase 4 — Extended Features

* Market Depth
* Liquidity Score
* VWAP
* Trade Intensity

Purpose:

Expand quantitative coverage.

---

## Phase 5 — Advanced Research

* VPIN
* Kyle's Lambda
* Roll Spread
* Amihud Illiquidity
* Order Book Slope

Purpose:

Research-grade market microstructure analysis.

---

# 12. Summary

The Feature Catalog defines the complete mathematical interface of the MarketPulse Intelligence Engine.

Every feature has a deterministic specification, explicit dependencies, known computational complexity, and a clearly defined implementation phase.

This document serves as the authoritative reference for all future feature development and guarantees that MarketPulse evolves through deliberate architectural decisions rather than ad hoc feature additions.
