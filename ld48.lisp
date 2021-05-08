(load "pre.lisp")
(load "audio.lisp")
(define show-side-view #f)
(if show-side-view
    (begin
     (set-camera -11 0 8.5 0 0.3 0)
     (perspective 2.0 1.0 0.1 10000))
    (begin
     (set-camera 0 0 0 0 0 0)
     (orthographic -25.0 25.0 -25.0 25.0 -50 50)
     ))


(define update
  (let ()
    (lambda ())))

(define unit-quad
    (model
     (poly -1 -1 0
	   1 -1 0
	   -1 1 0
	   1 1 0)
     ))

(show-model unit-quad)
