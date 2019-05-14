<?php

class Base {
  public function sayHello() {
    echo 'Hello ';
  }
}
trait SayWorld {
  public function sayHello() {
    parent::sayHello();
    echo 'World!';
  }
}
class MyHelloWorld extends Base {
  use SayWorld;
}
<<__EntryPoint>> function main() {
$o = new MyHelloWorld();
$o->sayHello();
}
