<?hh // strict
<<__Pure>>
function f()[]: int {
  if (Rx\IS_ENABLED) {
    return rx();
  } else {
    return nonrx();
  }
}

<<__Pure, __AtMostRxAsArgs>>
function f2(<<__AtMostRxAsFunc>> (function()[_]: void) $fn)[ctx $fn]: void {
  if (Rx\IS_ENABLED) {
    $fn();
  } else {
    $fn();
  }
}

interface IFoo {}
class TestClass {
  <<__Pure, __OnlyRxIfImpl(IFoo::class)>>
  public function f2(): void {
    if (Rx\IS_ENABLED) {} else {}
  }

  <<__Pure, __OnlyRxIfImpl(IFoo::class), __AtMostRxAsArgs>>
  public function f3(
    <<__AtMostRxAsFunc>> (function()[_]: void) $fn,
  )[ctx $fn]: void {
    if (Rx\IS_ENABLED) {
      $fn();
    } else {
      $fn();
    }
  }
}

<<__Pure>>
function rx()[]: int {
  return 1;
}

function nonrx(): int{
  return 1;
}
