(define (extend-env vars vals env) (cons (cons vars vals) env))
(define global (extend-env '() '() '()))
(define the-empty-environment '())

;;;(set-car! (car global) (cons '(x y) '(9 12)))
(set! global (extend-env '(x y z) '(9 17 37) global))
(set! global (extend-env '(t r e) '(39 414 139) global))


(define (lookup-variable-value var env)
	(define (env-loop env)
		(define (scan vars vals)
			(if (null? vars)
				(env-loop (cdr env))
				(if (eq var (car vars))
		 			(car vals)
					(scan (cdr vars) (cdr vals)))))
		(if (null? env)
			"Unbound variable"
			(let ((frame (car env)))
				(scan (car frame)
		    		(cdr frame)))))
	(env-loop env))

(define (add-binding-to-frame! var val frame)
  (set-car! frame (cons var (car frame)))
  (set-cdr! frame (cons val (cdr frame))))

(define (define-variable! var val env)
  (let ((frame (car env)))
    (define (scan vars vals)
      (if (null? vars)
          (add-binding-to-frame! var val frame)
          (if (eq? var (car vars))
              (set-car! vals val)
              (scan (cdr vars) (cdr vals))
           )
      )
  	)
    (scan (car frame)
          (cdr frame))))