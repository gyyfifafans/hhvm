<?php

trait TestTrait {
  public static function test() {
    return static::class;
  }
}

class A {
  use TestTrait;
}

class B extends A {}
<<__EntryPoint>> function main() {
echo B::test();
}
